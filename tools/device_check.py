#!/usr/bin/env python3
"""Repeat real captures/playback and check device logs; cannot prove audible sound."""
import argparse
from pathlib import Path
import re
import time

import serial

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--port', default='/dev/cu.usbserial-110')
p.add_argument('--cycles', type=int, default=3)
p.add_argument('--require-display', action='store_true', help='Also require successful photo transfers to the LCD')
p.add_argument('--out', type=Path, default=Path('build/device-check.log'))
args = p.parse_args()
if not 1 <= args.cycles <= 10:
    p.error('--cycles must be 1..10')
s = serial.Serial()
s.port, s.baudrate, s.timeout = args.port, 115200, .25
s.dtr = s.rts = False
s.open()
log = []

def until_status(timeout):
    lines = []
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        line = s.readline().decode('utf-8', 'replace').strip()
        if not line:
            continue
        print(line, flush=True)
        log.append(line)
        lines.append(line)
        if line.startswith(('ERROR', 'DISPLAY error', 'Guru Meditation', 'BUSY')):
            raise RuntimeError(line)
        if line.startswith('STATUS '):
            values = {k: int(v) for k, v in re.findall(r'(\w+)=(\d+)', line)}
            return lines, values
    raise RuntimeError('Timed out waiting for STATUS')

try:
    s.reset_input_buffer()
    s.write(b's')
    _, initial = until_status(4)
    assert initial['camera'] == initial['audio'] == 1, 'Peripherals not ready'
    assert initial['playing'] == 0, 'Device already playing'
    if args.require_display:
        assert initial.get('display') == 1, 'Display driver not ready'
    for cycle in range(args.cycles):
        s.write(b'c')
        lines, state = until_status(15)
        assert any(x.startswith('CAPTURE source=serial-test') for x in lines)
        assert any(x.startswith('SONG ') and 'notes=' in x for x in lines)
        assert 'PLAY complete' in lines, 'Playback did not complete'
        assert state['completed'] == initial['completed'] + cycle + 1, 'Unexpected completion count'
        assert state['playing'] == 0 and state['camera'] == state['audio'] == 1
        if args.require_display:
            assert state.get('display') == 1, 'Display failed during playback'
            assert state.get('photos') == initial['photos'] + cycle + 1, 'Photo was not submitted to the LCD'
            assert any(x.startswith('DISPLAY photo=') for x in lines)
    print(f'PASS {args.cycles} cycles; heap={state["heap"]}, psram={state["psram"]}; audibility requires listening')
finally:
    s.close()
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text('\n'.join(log) + '\n')
