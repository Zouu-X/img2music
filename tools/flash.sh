#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
if [ -z "${FLASH_PYTHON:-}" ]; then
  if [ -x .local/venv/bin/python ]; then
    FLASH_PYTHON="$PWD/.local/venv/bin/python"
  else
    FLASH_PYTHON=python3
  fi
fi
DEVICE_PROFILE=${DEVICE_PROFILE:-.local/device.json}
# Check recovery data before building or opening a serial port.
"$FLASH_PYTHON" tools/prepare_device.py --profile "$DEVICE_PROFILE" --check-profile
./tools/build.sh
set -- --profile "$DEVICE_PROFILE" \
  --flash-application build/v2m/v2m.ino.bin \
  --build-partitions build/v2m/v2m.ino.partitions.bin
if [ -n "${FLASH_PORT:-}" ]; then set -- "$@" --port "$FLASH_PORT"; fi
if [ -n "${FLASH_BAUD:-}" ]; then set -- "$@" --baud "$FLASH_BAUD"; fi
"$FLASH_PYTHON" tools/prepare_device.py "$@"
