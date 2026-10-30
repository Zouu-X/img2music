#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
./tools/build.sh
FLASH_PYTHON=${FLASH_PYTHON:-/Users/zoe/Projects/seirei/.local/venv/bin/python}
FLASH_PORT=${FLASH_PORT:-/dev/cu.usbserial-110}
"$FLASH_PYTHON" -m esptool --chip esp32s3 --port "$FLASH_PORT" --connect-attempts 2 --after no_reset flash_id > build/preflash-identify.log 2>&1
if ! rg -q 'MAC: d8:85:ac:d4:7e:fc' build/preflash-identify.log; then
  cat build/preflash-identify.log
  echo 'Target identity mismatch; refusing to flash.' >&2
  exit 1
fi
"$FLASH_PYTHON" -m esptool --chip esp32s3 --port "$FLASH_PORT" --baud 460800 --before no_reset --after no_reset read_flash 0x8000 0x1000 build/preflash-partitions.bin
"$FLASH_PYTHON" - <<'PY'
from pathlib import Path
import hashlib, struct
backup=Path('backups/factory-d885acd47efc-20261030.bin').read_bytes()
assert len(backup)==0x1000000
assert hashlib.sha256(backup).hexdigest()=='cd0f30ac9491da499eaf2167f07b5accf23cf6779c506110431aa766f346273e', 'Factory backup checksum mismatch'
def entries(data):
    return [struct.unpack('<HBBII16sI',data[i:i+32]) for i in range(0,0xc00,32) if data[i:i+2]==b'\xaa\x50']
factory=entries(backup[0x8000:0x9000])
assert entries(Path('build/preflash-partitions.bin').read_bytes())==factory, 'Unexpected device partitions'
assert entries(Path('build/v2m/v2m.ino.partitions.bin').read_bytes())==factory, 'Build partitions differ from factory'
assert any(e[1:5]==(0,0,0x10000,0x100000) for e in factory), 'Unexpected app partition'
assert Path('build/v2m/v2m.ino.bin').stat().st_size<=0x100000, 'Application exceeds 1 MiB'
PY
"$FLASH_PYTHON" -m esptool --chip esp32s3 --port "$FLASH_PORT" --baud 460800 --before no_reset --after no_reset write_flash 0x10000 build/v2m/v2m.ino.bin
"$FLASH_PYTHON" -m esptool --chip esp32s3 --port "$FLASH_PORT" --baud 460800 --before no_reset --after hard_reset verify_flash 0x10000 build/v2m/v2m.ino.bin
shasum -a 256 build/v2m/v2m.ino.bin > build/FIRMWARE-SHA256SUMS
