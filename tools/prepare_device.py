#!/usr/bin/env python3
"""Create/check a local recovery profile, or flash its factory application only.

Hardware is accessed only by explicit --backup or --flash-application actions.
--import-backup and --check-profile are entirely offline. No shell is invoked.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import importlib.metadata
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import uuid

ROOT = Path(__file__).resolve().parents[1]
FLASH_SIZE = 0x1000000
TABLE_OFFSET, TABLE_SIZE = 0x8000, 0x1000
APP_OFFSET, APP_SIZE = 0x10000, 0x100000
SUPPORTED_PARTITIONS = [
    dict(type=1, subtype=2, offset=0x9000, size=0x6000, label='nvs', flags=0),
    dict(type=1, subtype=1, offset=0xf000, size=0x1000, label='phy_init', flags=0),
    dict(type=0, subtype=0, offset=APP_OFFSET, size=APP_SIZE, label='factory', flags=0),
]


class ProfileError(ValueError):
    pass


def fail(message):
    raise ProfileError(message)


def mac_address(value):
    if not isinstance(value, str) or not re.fullmatch(r'(?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}', value):
        fail('Missing or invalid chip MAC address.')
    return value.lower()


def digest(value):
    if not isinstance(value, str) or not re.fullmatch(r'[0-9a-fA-F]{64}', value):
        fail('Missing or invalid SHA-256 digest.')
    return value.lower()


def inside(path, directory):
    try:
        path.relative_to(directory)
        return True
    except ValueError:
        return False


def local_path(value, root=ROOT, kind='backup'):
    root = Path(root).resolve()
    value = Path(value).expanduser()
    path = (value if value.is_absolute() else root / value).resolve()
    # Resolve first: a symlink must not escape the repository either.
    allowed = [root / '.local'] if kind == 'profile' else [root / 'backups', root / '.local/backups']
    if not any(inside(path, directory) and path != directory for directory in allowed):
        fail('{} must stay under {}.'.format(kind.capitalize(), ' or '.join(str(p) for p in allowed)))
    if kind == 'profile' and (path.suffix.lower() != '.json' or inside(path, root / '.local/backups')):
        fail('Profile must be a .json file under .local/, outside .local/backups/.')
    return path


def backup_directory(value, root=ROOT):
    root = Path(root).resolve()
    value = Path(value).expanduser()
    path = (value if value.is_absolute() else root / value).resolve()
    if not any(inside(path, directory) for directory in [root / 'backups', root / '.local/backups']):
        fail('Backup directory must stay inside .local/backups/ or backups/.')
    return path


def sha256_file(path):
    result = hashlib.sha256()
    with Path(path).open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()


def parse_partitions(data):
    if len(data) < 0xc00:
        fail('Partition table is truncated (expected at least 3072 bytes).')
    entries = []
    for offset in range(0, 0xc00, 32):
        record = data[offset:offset + 32]
        magic = struct.unpack_from('<H', record)[0]
        if magic == 0x50aa:
            _, kind, subtype, start, size, label, flags = struct.unpack('<HBBII16sI', record)
            try:
                label = label.split(b'\0', 1)[0].decode('ascii')
            except UnicodeDecodeError:
                fail('Partition label is not ASCII.')
            if not size or start + size > FLASH_SIZE:
                fail('Partition lies outside the supported 16 MiB flash.')
            entries.append(dict(type=kind, subtype=subtype, offset=start, size=size, label=label, flags=flags))
        elif magic == 0xebeb:
            if record[:16] != b'\xeb\xeb' + b'\xff' * 14:
                fail('Malformed partition checksum record.')
            if record[16:] != hashlib.md5(data[:offset]).digest():
                fail('Partition table MD5 mismatch.')
            if any(byte != 0xff for byte in data[offset + 32:0xc00]):
                fail('Unexpected records after the partition checksum.')
            if not entries:
                fail('Partition table has no entries.')
            return entries
        else:
            fail('Partition table missing its checksum or contains an unknown record.')
    fail('Partition table contains no checksum record.')


def supported_layout(entries):
    if entries != SUPPORTED_PARTITIONS:
        fail('Unsupported partition layout. Expected verified nvs/phy_init/factory with '
             'factory at 0x10000, size 1 MiB. Keep the backup and ask the Agent to '
             'analyze this board; do not erase flash or weaken this check.')


def read_json(path):
    try:
        value = json.loads(Path(path).read_text(encoding='utf-8'))
    except (OSError, ValueError) as error:
        fail('Cannot read JSON {}: {}'.format(path, error))
    if not isinstance(value, dict):
        fail('Expected a JSON object in {}.'.format(path))
    return value


def validate_backup(path, expected_digest, expected_size=FLASH_SIZE):
    if expected_size != FLASH_SIZE or Path(path).stat().st_size != FLASH_SIZE:
        fail('Backup must contain the full supported 16 MiB flash.')
    if sha256_file(path) != digest(expected_digest):
        fail('Backup SHA-256 mismatch; refusing to proceed.')
    with Path(path).open('rb') as source:
        source.seek(TABLE_OFFSET)
        table = source.read(TABLE_SIZE)
    return table, parse_partitions(table)


def make_profile(mac, backup, backup_sha256, table, entries, port=None,
                 verification='imported_checksum_only', root=ROOT):
    supported_layout(entries)
    backup = local_path(backup, root)
    return dict(schema_version=1, chip='esp32s3', mac=mac_address(mac),
                flash_size=FLASH_SIZE, port_hint=port,
                created_at=datetime.now(timezone.utc).isoformat(),
                backup=dict(file=str(backup.relative_to(Path(root).resolve())),
                            bytes=FLASH_SIZE, sha256=digest(backup_sha256),
                            verification=verification),
                partition_table_sha256=hashlib.sha256(table).hexdigest(),
                partitions=entries)


def validate_profile(profile_path, root=ROOT):
    profile = read_json(local_path(profile_path, root, 'profile'))
    if profile.get('schema_version') != 1 or profile.get('chip') != 'esp32s3':
        fail('Unsupported profile schema or chip; prepare this ESP32-S3 explicitly.')
    profile['mac'] = mac_address(profile.get('mac'))
    if profile.get('flash_size') != FLASH_SIZE:
        fail('Only the verified 16 MiB board configuration is supported.')
    info = profile.get('backup')
    if not isinstance(info, dict) or not isinstance(info.get('file'), str):
        fail('Profile has no backup file.')
    if info.get('verification') not in ('read_and_verified', 'imported_checksum_only'):
        fail('Profile has no recognized backup provenance.')
    table, entries = validate_backup(local_path(info['file'], root), info.get('sha256'), info.get('bytes'))
    supported_layout(entries)
    if profile.get('partitions') != entries:
        fail('Profile partitions differ from its backup.')
    if digest(profile.get('partition_table_sha256')) != hashlib.sha256(table).hexdigest():
        fail('Profile partition digest differs from its backup.')
    return profile, table


def write_profile(path, profile, force=False, root=ROOT):
    path = local_path(path, root, 'profile')
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and not force:
        fail('Profile already exists. Use another --profile or explicit --force; backups are never overwritten.')
    encoded = json.dumps(profile, indent=2) + '\n'
    if force:
        temporary = path.with_name(path.name + '.' + uuid.uuid4().hex + '.tmp')
        with temporary.open('x', encoding='utf-8') as destination:
            destination.write(encoded)
        os.replace(temporary, path)
    else:
        with path.open('x', encoding='utf-8') as destination:
            destination.write(encoded)
    return path


def import_backup(metadata_path, profile_path, port=None, force=False, root=ROOT):
    metadata_path = local_path(metadata_path, root)
    metadata = read_json(metadata_path)
    if not str(metadata.get('chip', '')).startswith('ESP32-S3'):
        fail('Backup metadata does not identify an ESP32-S3.')
    filename = metadata.get('file')
    if not isinstance(filename, str) or Path(filename).name != filename:
        fail('Backup metadata file must be a sibling basename, not a path.')
    backup = local_path(metadata_path.parent / filename, root)
    table, entries = validate_backup(backup, metadata.get('sha256'), metadata.get('bytes'))
    recorded = metadata.get('partitions')
    if not isinstance(recorded, list):
        fail('Backup metadata has no partition list.')
    try:
        normalized = [dict(type=int(p['type']), subtype=int(p['subtype']),
                           offset=int(str(p['offset']), 0), size=int(str(p['size']), 0),
                           label=p['label'], flags=int(p.get('flags', 0))) for p in recorded]
    except (TypeError, ValueError, KeyError):
        fail('Malformed backup partition metadata.')
    if normalized != entries:
        fail('Backup bytes differ from the recorded partition metadata.')
    profile = make_profile(metadata.get('mac'), backup, metadata.get('sha256'),
                           table, entries, port, root=root)
    return write_profile(profile_path, profile, force, root)


def identify(output):
    if not re.search(r'^Chip is ESP32-S3(?:\s|\(|$)', output, re.MULTILINE):
        fail('esptool did not identify the target as ESP32-S3.')
    mac = re.search(r'^MAC:\s*([0-9a-fA-F:]{17})\s*$', output, re.MULTILINE)
    flash = re.search(r'^Detected flash size:\s*(\d+)\s*MB\s*$', output, re.MULTILINE)
    if not mac or not flash:
        fail('Cannot parse chip MAC and flash size from esptool 4.12.0 output.')
    size = int(flash.group(1)) * 1024 * 1024
    if size != FLASH_SIZE:
        fail('Detected flash is not 16 MiB; this board needs separate analysis before flashing.')
    return dict(chip='esp32s3', mac=mac_address(mac.group(1)), flash_size=size)


def match_identity(profile, actual):
    for key in ('chip', 'mac', 'flash_size'):
        if profile[key] != actual[key]:
            fail('Connected device {} does not match this profile; refusing to flash.'.format(key))


def check_flash_inputs(profile_path, application, build_partitions, root=ROOT):
    profile, table = validate_profile(profile_path, root)
    application = Path(application)
    if not 0 < application.stat().st_size <= APP_SIZE:
        fail('Application is empty or exceeds the 1 MiB factory partition.')
    with application.open('rb') as source:
        if source.read(1) != b'\xe9':
            fail('Application does not start with an ESP image header.')
    entries = parse_partitions(Path(build_partitions).read_bytes())
    supported_layout(entries)
    if entries != profile['partitions']:
        fail('Build partition layout differs from the recovery profile.')
    return profile, table


def check_live_partitions(profile, backup_table, live_table):
    entries = parse_partitions(live_table)
    supported_layout(entries)
    if entries != profile['partitions'] or live_table != backup_table:
        fail('Connected board partition bytes differ from its recovery backup; refusing to flash.')


class EspTool:
    def __init__(self, port, baud):
        if not isinstance(port, str) or not port or port.startswith('-'):
            fail('An explicit --port, FLASH_PORT, or profile port_hint is required.')
        try:
            version = importlib.metadata.version('esptool')
        except importlib.metadata.PackageNotFoundError:
            fail('Install requirements.txt in this Python environment before accessing hardware.')
        if version != '4.12.0':
            fail('Expected esptool 4.12.0; install the pinned requirements.txt in this Python environment.')
        self.port, self.baud = port, baud

    def run(self, command, before='no_reset', after='no_reset', timeout=None):
        if timeout is None:
            timeout = 1200
            if command[0] == 'read_flash':
                # 16 MiB already takes >24 minutes at 115200 baud before
                # protocol overhead. Scale the budget for supported slow links.
                size = int(str(command[2]), 0)
                timeout = max(timeout, int(size * 14 / self.baud) + 180)
        argv = [sys.executable, '-m', 'esptool', '--chip', 'esp32s3', '--port', self.port,
                '--baud', str(self.baud), '--connect-attempts', '2', '--before', before,
                '--after', after] + [str(value) for value in command]
        result = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, timeout=timeout, check=False)
        if result.returncode:
            # esptool diagnostics only; never print bytes from a flash dump.
            fail('esptool failed ({}):\n{}'.format(command[0], '\n'.join(result.stdout.splitlines()[-15:])))
        return result.stdout

    def identity(self):
        return identify(self.run(['flash_id'], before='default_reset', timeout=60))


def prepare_backup(args):
    profile_path = local_path(args.profile, kind='profile')
    if profile_path.exists() and not args.force:
        fail('Profile already exists; choose --profile or explicit --force before accessing hardware.')
    directory = backup_directory(args.backup_dir)
    directory.mkdir(parents=True, exist_ok=True)
    tool = EspTool(args.port, args.baud)
    actual = tool.identity()
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    name = 'factory-{}-{}-{}'.format(actual['mac'].replace(':', ''), stamp, uuid.uuid4().hex[:8])
    partial = directory / (name + '.partial.bin')
    with partial.open('xb'):
        pass
    print('Reading full 16 MiB flash; this can take several minutes.', flush=True)
    tool.run(['read_flash', '0x0', hex(FLASH_SIZE), partial])
    if partial.stat().st_size != FLASH_SIZE:
        fail('Flash read has the wrong size; partial backup kept, no profile created.')
    print('Comparing the full backup against the board before creating its profile.', flush=True)
    tool.run(['verify_flash', '0x0', partial], after='hard_reset')
    backup = directory / (name + '.bin')
    if backup.exists():
        fail('Backup name collision; no existing backup will be overwritten.')
    partial.rename(backup)
    backup_sha256 = sha256_file(backup)
    print('Verified backup saved: {}'.format(backup.relative_to(ROOT)), flush=True)
    with backup.open('rb') as source:
        source.seek(TABLE_OFFSET)
        table = source.read(TABLE_SIZE)
    partition_error = None
    try:
        entries = parse_partitions(table)
    except ProfileError as error:
        entries = None
        partition_error = str(error)
    metadata = dict(date=datetime.now(timezone.utc).isoformat(), mac=actual['mac'],
                    chip='ESP32-S3', bytes=FLASH_SIZE, sha256=backup_sha256,
                    baud=args.baud, file=backup.name, partitions=entries,
                    verification='read_and_verified')
    if partition_error:
        metadata['partition_error'] = partition_error
    with backup.with_suffix('.json').open('x', encoding='utf-8') as destination:
        json.dump(metadata, destination, indent=2)
        destination.write('\n')
    if partition_error:
        fail('Full backup and metadata kept, but no profile created: ' + partition_error)
    profile = make_profile(actual['mac'], backup, backup_sha256, table, entries,
                           args.port, 'read_and_verified')
    return write_profile(profile_path, profile, args.force)


def flash_application(args):
    if not args.build_partitions:
        fail('--flash-application requires --build-partitions from the same build.')
    profile, backup_table = check_flash_inputs(args.profile, args.flash_application, args.build_partitions)
    tool = EspTool(args.port or profile.get('port_hint'), args.baud)
    match_identity(profile, tool.identity())
    build = ROOT / 'build'
    build.mkdir(exist_ok=True)
    live = build / 'preflash-partitions.bin'
    tool.run(['read_flash', hex(TABLE_OFFSET), hex(TABLE_SIZE), live])
    check_live_partitions(profile, backup_table, live.read_bytes())
    print('Identity, backup and partitions match. Writing factory application only.', flush=True)
    tool.run(['write_flash', hex(APP_OFFSET), args.flash_application])
    tool.run(['verify_flash', hex(APP_OFFSET), args.flash_application], after='hard_reset')
    image_hash = sha256_file(args.flash_application)
    (build / 'FIRMWARE-SHA256SUMS').write_text('{}  {}\n'.format(image_hash, args.flash_application))
    print('Application verified and board restarted. SHA-256: {}'.format(image_hash))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument('--backup', action='store_true', help='Read and verify full flash, then create local profile.')
    action.add_argument('--import-backup', type=Path, metavar='JSON', help='Offline import of your existing backup metadata.')
    action.add_argument('--check-profile', action='store_true', help='Offline profile, backup and partition verification.')
    action.add_argument('--flash-application', type=Path, metavar='BIN', help='Used by flash.sh; guarded factory-app write only.')
    parser.add_argument('--profile', type=Path, default=Path(os.environ.get('DEVICE_PROFILE', '.local/device.json')))
    parser.add_argument('--port', default=os.environ.get('FLASH_PORT'))
    parser.add_argument('--baud', type=int, choices=(115200, 230400, 460800, 921600), default=460800)
    parser.add_argument('--backup-dir', type=Path, default=Path('.local/backups'))
    parser.add_argument('--build-partitions', type=Path)
    parser.add_argument('--force', action='store_true', help='Replace profile JSON only; never overwrite a dump.')
    args = parser.parse_args(argv)
    try:
        if args.backup:
            path = prepare_backup(args)
            print('Device profile created: {}'.format(path.relative_to(ROOT)))
        elif args.import_backup:
            path = import_backup(args.import_backup, args.profile, args.port, args.force)
            print('Imported backup checksum/layout verified offline: {}'.format(path.relative_to(ROOT)))
            print('Device identity will be checked again before any application write.')
        elif args.check_profile:
            profile, _ = validate_profile(args.profile)
            print('Recovery profile valid (offline): ESP32-S3, 16 MiB, factory app 0x10000 / 1 MiB.')
            print('Backup provenance: {}'.format(profile['backup']['verification']))
        else:
            flash_application(args)
    except (ProfileError, OSError, subprocess.TimeoutExpired) as error:
        print('ERROR: {}'.format(error), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
