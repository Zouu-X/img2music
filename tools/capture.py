#!/usr/bin/env python3
"""Request one fresh JPEG over UART for visual inspection; no network or flash writes."""
import argparse
from pathlib import Path
import time
import serial

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--port', default='/dev/cu.usbserial-110')
p.add_argument('--out', type=Path, default=Path('build/camera.jpg'))
args = p.parse_args()
s = serial.Serial()
s.port, s.baudrate, s.timeout = args.port, 115200, .5
s.dtr = s.rts = False
s.open()
try:
    s.reset_input_buffer()
    s.write(b'j')
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        line = s.readline()
        if line.startswith(b'ERROR'):
            raise RuntimeError(line.decode('ascii', 'replace').strip())
        if line.startswith(b'JPEG ') and line.strip().split()[-1].isdigit():
            length = int(line.strip().split()[-1])
            if not 4 <= length <= 256 * 1024:
                raise RuntimeError(f'Invalid JPEG length: {length}')
            image = bytearray()
            deadline = time.monotonic() + 30
            while len(image) < length and time.monotonic() < deadline:
                image.extend(s.read(length - len(image)))
            if len(image) != length or image[:2] != b'\xff\xd8' or image[-2:] != b'\xff\xd9':
                raise RuntimeError('Incomplete or invalid JPEG transfer')
            args.out.parent.mkdir(parents=True, exist_ok=True)
            args.out.write_bytes(image)
            print(f'Saved {length} bytes to {args.out}')
            break
    else:
        raise RuntimeError('No JPEG header within 15 seconds')
finally:
    s.close()
