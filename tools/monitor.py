#!/usr/bin/env python3
"""Bounded UART capture. Opens with reset lines inactive; never flashes."""
import argparse
import time
from pathlib import Path

import serial

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--port', default='/dev/cu.usbserial-110')
p.add_argument('--seconds', type=float, default=10)
p.add_argument('--send', default='')
p.add_argument('--out', type=Path)
args = p.parse_args()
if not 0 < args.seconds <= 300:
    p.error('--seconds must be in (0, 300]')
s = serial.Serial()
s.port, s.baudrate, s.timeout = args.port, 115200, .2
s.dtr = s.rts = False
s.open()
parts = []
try:
    if args.send:
        s.write(args.send.encode('ascii'))
    end = time.monotonic() + args.seconds
    while time.monotonic() < end:
        chunk = s.read(4096)
        if chunk:
            parts.append(chunk)
            print(chunk.decode('utf-8', 'replace'), end='', flush=True)
finally:
    s.close()
if args.out:
    args.out.write_bytes(b''.join(parts))

