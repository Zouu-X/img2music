#!/usr/bin/env python3
"""Exercise replay, live drums and stop on the board; physical keys need a person."""
import argparse
from pathlib import Path
import re
import time

import serial

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--port', default='/dev/cu.usbserial-110')
parser.add_argument('--out', type=Path, default=Path('build/controls-check.log'))
args = parser.parse_args()
log = []
device = serial.Serial()
device.port, device.baudrate, device.timeout = args.port, 115200, .1
device.dtr = device.rts = False


def wait_for(prefix, timeout=15):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        line = device.readline().decode('utf-8', 'replace').strip()
        if not line:
            continue
        log.append(line)
        print(line, flush=True)
        if line.startswith(('ERROR', 'DISPLAY error', 'Guru Meditation', 'BUSY')):
            raise RuntimeError(line)
        if line.startswith(prefix):
            return line
    raise RuntimeError(f'Timed out waiting for {prefix}')


def send(command, prefix, timeout=15):
    device.write(command.encode('ascii'))
    return wait_for(prefix, timeout)


def state(line):
    return {key: int(value) for key, value in re.findall(r'(\w+)=(\d+)', line)}


def finished(previous):
    wait_for('PLAY complete')
    current = state(wait_for('STATUS '))
    assert current['completed'] == previous['completed'] + 1, current
    assert current['camera'] == current['audio'] == current['display'] == 1, current
    assert current['playing'] == current['underruns'] == 0, current
    return current


try:
    device.open()
    device.reset_input_buffer()
    initial = state(send('s', 'STATUS '))
    assert initial['playing'] == 0, 'Device is playing; wait for it to finish first'
    assert initial['camera'] == initial['audio'] == initial['display'] == 1
    if initial['drums']:
        send('d', 'DRUMS enabled=0')
    send('c', 'PLAY begin')
    time.sleep(.9)
    send('d', 'DRUMS enabled=1')
    time.sleep(.9)
    send('d', 'DRUMS enabled=0')
    first = finished(initial)
    assert first['photos'] == initial['photos'] + 1

    send('d', 'DRUMS enabled=1')
    send('p', 'PLAY begin source=serial-replay drums=1')
    time.sleep(.9)
    send('p', 'PLAY begin source=serial-replay drums=1')
    second = finished(first)
    assert second['photos'] == first['photos'], 'Replay unexpectedly took a photo'

    send('p', 'PLAY begin')
    time.sleep(.6)
    send('x', 'PLAY stopped')
    stopped = state(send('s', 'STATUS '))
    assert stopped['playing'] == 0 and stopped['completed'] == second['completed']
    send('d', 'DRUMS enabled=0')
    send('p', 'PLAY begin source=serial-replay drums=0')
    third = finished(second)
    assert third['photos'] == first['photos']
    assert third['heap'] == first['heap'] and third['psram'] == first['psram']
    print('PASS: live drums on/off, replay, mid-song restart, stop/replay, display and audio clock; physical keys/listening still require confirmation')
finally:
    device.close()
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text('\n'.join(log) + '\n')
