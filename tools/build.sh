#!/bin/sh
set -eu
cd "$(dirname "$0")/.."

if [ -z "${ARDUINO:-}" ]; then
  if [ -x .local/bin/arduino-cli ]; then
    ARDUINO="$PWD/.local/bin/arduino-cli"
  elif command -v arduino-cli >/dev/null 2>&1; then
    ARDUINO=arduino-cli
  else
    echo 'Arduino CLI not found. Install it, add it to PATH, or set ARDUINO to its executable.' >&2
    exit 1
  fi
fi
BUILD_PYTHON=${PYTHON:-python3}
mkdir -p .local/downloads .local/user build

# Explicit ARDUINO_DATA wins, then project-local installation, then the CLI's
# own configured/default directory. This script never installs or updates cores.
if [ -z "${ARDUINO_DATA:-}" ] && [ ! -d .local/arduino-data/packages ]; then
  if ! "$ARDUINO" config dump --format json > .local/arduino-defaults.json; then
    echo 'Cannot read Arduino CLI configuration. Set ARDUINO_DATA to the directory containing packages/.' >&2
    exit 1
  fi
fi
export ARDUINO_DATA
"$BUILD_PYTHON" - <<'PY'
import json
import os
from pathlib import Path
root = Path.cwd()
data = os.environ.get('ARDUINO_DATA')
if not data and (root / '.local/arduino-data/packages').is_dir():
    data = str(root / '.local/arduino-data')
if not data:
    defaults = json.loads(Path('.local/arduino-defaults.json').read_text())
    defaults = defaults.get('config', defaults)
    data = defaults.get('directories', {}).get('data')
if not data:
    raise SystemExit('Arduino CLI did not report directories.data; set ARDUINO_DATA explicitly.')
data = Path(data).expanduser().resolve()
if not (data / 'packages/esp32/hardware/esp32').is_dir():
    raise SystemExit('ESP32 Arduino core missing in ' + str(data) +
                     '. Install esp32:esp32@3.3.10 there; see docs/SETUP.md.')
config = {'directories': {
    'data': str(data), 'downloads': str(root / '.local/downloads'),
    'user': str(root / '.local/user'),
}, 'build_cache': {'path': str(root / '.local/cache')}}
Path('.local/arduino-cli.yaml').write_text(json.dumps(config, indent=2) + '\n')
PY
"$ARDUINO" --config-file .local/arduino-cli.yaml core list --format json > .local/arduino-cores.json
"$BUILD_PYTHON" - <<'PY'
import json
from pathlib import Path
reported = json.loads(Path('.local/arduino-cores.json').read_text())
platforms = reported.get('platforms', []) if isinstance(reported, dict) else []
versions = [p.get('installed_version') for p in platforms if p.get('id') == 'esp32:esp32']
if versions != ['3.3.10']:
    raise SystemExit('Build requires installed esp32:esp32@3.3.10; CLI reported ' + repr(versions) +
                     '. Install the tested version in the selected ARDUINO_DATA directory.')
PY
"$ARDUINO" --config-file .local/arduino-cli.yaml compile \
  --fqbn 'esp32:esp32:esp32s3:FlashSize=16M,FlashMode=dio,PartitionScheme=custom,PSRAM=opi,CDCOnBoot=default,USBMode=hwcdc' \
  --build-path build/obj --output-dir build/v2m firmware/v2m
"$BUILD_PYTHON" - <<'PY'
from pathlib import Path
image = Path('build/v2m/v2m.ino.bin')
if not 0 < image.stat().st_size <= 0x100000:
    raise SystemExit('Firmware is empty or exceeds the preserved 1 MiB factory partition.')
PY
