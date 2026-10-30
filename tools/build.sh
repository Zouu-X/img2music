#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
ARDUINO=${ARDUINO:-/Users/zoe/Projects/seirei/.local/bin/arduino-cli}
ARDUINO_DATA=${ARDUINO_DATA:-/Users/zoe/Projects/seirei/.local/arduino-data}
mkdir -p .local/downloads .local/user build
export ARDUINO_DATA
python3 - <<'PY'
import json, os
from pathlib import Path
root = Path.cwd()
config = {'directories': {
    'data': os.environ['ARDUINO_DATA'],
    'downloads': str(root / '.local/downloads'),
    'user': str(root / '.local/user'),
}, 'build_cache': {'path': str(root / '.local/cache')}}
Path('.local/arduino-cli.yaml').write_text(json.dumps(config))
PY
"$ARDUINO" --config-file .local/arduino-cli.yaml compile \
  --fqbn 'esp32:esp32:esp32s3:FlashSize=16M,FlashMode=dio,PartitionScheme=custom,PSRAM=opi,CDCOnBoot=default,USBMode=hwcdc' \
  --build-path build/obj --output-dir build/v2m firmware/v2m
python3 - <<'PY'
from pathlib import Path
image = Path('build/v2m/v2m.ino.bin')
assert image.stat().st_size <= 0x100000, 'Firmware exceeds preserved 1 MiB factory partition'
PY

