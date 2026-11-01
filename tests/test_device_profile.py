"""Offline tests using synthetic flash fixtures, never evidence of real hardware."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location(
    'prepare_device', Path(__file__).resolve().parents[1] / 'tools/prepare_device.py')
device = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(device)


def partition_bytes(entries):
    payload = b''.join(struct.pack('<HBBII16sI', 0x50aa, entry['type'], entry['subtype'],
                                  entry['offset'], entry['size'], entry['label'].encode(),
                                  entry['flags']) for entry in entries)
    table = payload + b'\xeb\xeb' + b'\xff' * 14 + hashlib.md5(payload).digest()
    return table + b'\xff' * (device.TABLE_SIZE - len(table))


class DeviceProfileTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        (self.root / 'backups').mkdir()
        (self.root / '.local').mkdir()
        self.backup = self.root / 'backups/synthetic.bin'
        self.table = partition_bytes(device.SUPPORTED_PARTITIONS)
        with self.backup.open('wb') as destination:
            destination.truncate(device.FLASH_SIZE)
            destination.seek(device.TABLE_OFFSET)
            destination.write(self.table)
        self.sha = device.sha256_file(self.backup)
        self.profile_path = self.root / '.local/device.json'
        self.profile = device.make_profile(
            '02:00:00:00:00:01', self.backup, self.sha, self.table,
            copy.deepcopy(device.SUPPORTED_PARTITIONS), 'COM7', root=self.root)
        device.write_profile(self.profile_path, self.profile, root=self.root)

    def write_profile(self, value):
        self.profile_path.write_text(json.dumps(value))

    def test_valid_profile_and_portability(self):
        profile, table = device.validate_profile(self.profile_path, root=self.root)
        self.assertEqual(table, self.table)
        self.assertEqual(profile['backup']['file'], 'backups/synthetic.bin')
        self.assertEqual(profile['backup']['verification'], 'imported_checksum_only')
        self.assertEqual(profile['mac'], '02:00:00:00:00:01')

    def test_backup_digest_mismatch_is_rejected(self):
        with self.backup.open('r+b') as destination:
            destination.seek(device.FLASH_SIZE - 1)
            destination.write(b'\x01')
        with self.assertRaisesRegex(device.ProfileError, 'SHA-256 mismatch'):
            device.validate_profile(self.profile_path, root=self.root)

    def test_partial_backup_is_rejected(self):
        with self.backup.open('r+b') as destination:
            destination.truncate(device.FLASH_SIZE - 1)
        with self.assertRaisesRegex(device.ProfileError, 'full supported 16 MiB'):
            device.validate_profile(self.profile_path, root=self.root)

    def test_profile_cannot_change_device_or_backup_requirements(self):
        cases = [('schema_version', 2), ('chip', 'esp32'), ('flash_size', 0x800000),
                 ('mac', 'invalid'), ('partition_table_sha256', '0' * 64)]
        for key, value in cases:
            with self.subTest(key=key):
                altered = copy.deepcopy(self.profile)
                altered[key] = value
                self.write_profile(altered)
                with self.assertRaises(device.ProfileError):
                    device.validate_profile(self.profile_path, root=self.root)

    def test_partition_md5_and_truncation_are_checked(self):
        altered = bytearray(self.table)
        altered[16] ^= 1
        with self.assertRaisesRegex(device.ProfileError, 'MD5 mismatch'):
            device.parse_partitions(altered)
        with self.assertRaisesRegex(device.ProfileError, 'truncated'):
            device.parse_partitions(self.table[:100])
        missing_checksum = self.table[:96] + b'\xff' * (device.TABLE_SIZE - 96)
        with self.assertRaisesRegex(device.ProfileError, 'missing its checksum'):
            device.parse_partitions(missing_checksum)

    def test_other_valid_partition_layout_is_rejected(self):
        for key, value in [('offset', 0x20000), ('size', 0x200000), ('subtype', 0x10), ('flags', 1)]:
            with self.subTest(key=key):
                altered = copy.deepcopy(device.SUPPORTED_PARTITIONS)
                altered[-1][key] = value
                parsed = device.parse_partitions(partition_bytes(altered))
                with self.assertRaisesRegex(device.ProfileError, 'Unsupported partition layout'):
                    device.supported_layout(parsed)
        changed_nvs = copy.deepcopy(device.SUPPORTED_PARTITIONS)
        changed_nvs[0]['size'] = 0x5000
        with self.assertRaises(device.ProfileError):
            device.supported_layout(changed_nvs)

    def test_live_table_matches_exact_recovery_bytes(self):
        device.check_live_partitions(self.profile, self.table, self.table)
        altered = bytearray(self.table)
        altered[-1] = 0  # Same parsed entries, different reserved bytes.
        with self.assertRaisesRegex(device.ProfileError, 'partition bytes differ'):
            device.check_live_partitions(self.profile, self.table, bytes(altered))

    def test_identity_mismatch_is_rejected(self):
        actual = dict(chip='esp32s3', mac='02:00:00:00:00:01', flash_size=device.FLASH_SIZE)
        device.match_identity(self.profile, actual)
        for key, value in [('mac', '02:00:00:00:00:02'), ('chip', 'esp32'), ('flash_size', 0x800000)]:
            with self.subTest(key=key):
                changed = dict(actual, **{key: value})
                with self.assertRaisesRegex(device.ProfileError, 'does not match'):
                    device.match_identity(self.profile, changed)

    def test_esptool_text_parser_requires_all_identifiers(self):
        # This is a parser fixture, not captured or simulated device evidence.
        synthetic = ('Chip is ESP32-S3 (QFN56) (revision v0.2)\n'
                     'MAC: 02:00:00:00:00:01\nDetected flash size: 16MB\n')
        self.assertEqual(device.identify(synthetic)['mac'], '02:00:00:00:00:01')
        for altered in [synthetic.replace('ESP32-S3', 'ESP32'),
                        synthetic.replace('16MB', '8MB'), synthetic.replace('MAC:', 'Other:')]:
            with self.assertRaises(device.ProfileError):
                device.identify(altered)

    def test_paths_cannot_escape_repository_recovery_directories(self):
        for path in ['../outside.bin', 'README.md', '.local/elsewhere.bin']:
            with self.subTest(path=path), self.assertRaises(device.ProfileError):
                device.local_path(path, self.root)
        with self.assertRaises(device.ProfileError):
            device.local_path('device.json', self.root, 'profile')
        for profile_path in ['.local/backups/record.json', '.local/backups/dump.bin', '.local/dump.bin']:
            with self.subTest(profile_path=profile_path), self.assertRaises(device.ProfileError):
                device.local_path(profile_path, self.root, 'profile')
        outside = self.root / 'outside.bin'
        outside.write_bytes(b'not a recovery dump')
        link = self.root / 'backups/link.bin'
        try:
            link.symlink_to(outside)
        except (OSError, NotImplementedError):
            self.skipTest('Symlink creation unavailable on this host.')
        with self.assertRaises(device.ProfileError):
            device.local_path(link, self.root)

    def test_profile_replace_requires_explicit_force_and_preserves_backup(self):
        original = self.profile_path.read_bytes()
        with self.assertRaisesRegex(device.ProfileError, 'already exists'):
            device.write_profile(self.profile_path, self.profile, root=self.root)
        self.assertEqual(self.profile_path.read_bytes(), original)
        altered = dict(self.profile, port_hint='COM8')
        device.write_profile(self.profile_path, altered, force=True, root=self.root)
        self.assertEqual(json.loads(self.profile_path.read_text())['port_hint'], 'COM8')
        self.assertEqual(device.sha256_file(self.backup), self.sha)

    def test_legacy_metadata_import_is_offline_and_preserves_originals(self):
        metadata = dict(chip='ESP32-S3 rev v0.2', mac=self.profile['mac'], bytes=device.FLASH_SIZE,
                        sha256=self.sha, file=self.backup.name,
                        partitions=[dict(type=p['type'], subtype=p['subtype'], offset=hex(p['offset']),
                                         size=hex(p['size']), label=p['label']) for p in device.SUPPORTED_PARTITIONS])
        metadata_path = self.root / 'backups/synthetic.json'
        metadata_path.write_text(json.dumps(metadata))
        original = metadata_path.read_bytes()
        imported = self.root / '.local/imported.json'
        device.import_backup(metadata_path, imported, root=self.root)
        device.validate_profile(imported, root=self.root)
        self.assertEqual(metadata_path.read_bytes(), original)
        self.assertEqual(device.sha256_file(self.backup), self.sha)
        metadata['file'] = '../outside.bin'
        metadata_path.write_text(json.dumps(metadata))
        with self.assertRaisesRegex(device.ProfileError, 'sibling basename'):
            device.import_backup(metadata_path, imported, force=True, root=self.root)

    def test_build_preflight_rejects_wrong_table_or_oversize_app(self):
        app = self.root / 'app.bin'
        app.write_bytes(b'\xe9' + b'\0' * 63)
        table_file = self.root / 'partitions.bin'
        table_file.write_bytes(self.table[:0xc00])
        device.check_flash_inputs(self.profile_path, app, table_file, root=self.root)
        other = copy.deepcopy(device.SUPPORTED_PARTITIONS)
        other[-1]['offset'] = 0x20000
        table_file.write_bytes(partition_bytes(other))
        with self.assertRaisesRegex(device.ProfileError, 'Unsupported partition layout'):
            device.check_flash_inputs(self.profile_path, app, table_file, root=self.root)
        table_file.write_bytes(self.table)
        with app.open('r+b') as destination:
            destination.truncate(device.APP_SIZE + 1)
        with self.assertRaisesRegex(device.ProfileError, 'exceeds'):
            device.check_flash_inputs(self.profile_path, app, table_file, root=self.root)


if __name__ == '__main__':
    unittest.main()
