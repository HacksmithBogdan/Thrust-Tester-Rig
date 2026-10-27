"""Diagnostics for the LCD board's computer-connected USB service mode.

Requires pyserial (included in the build-tools environment). Does not synthesize
measurements. prepare-fresh-sd can format an unmountable card; use deliberately.
"""
import argparse
import json
from pathlib import Path
import time
import serial


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--port', default='COM3')
    parser.add_argument('--output', type=Path)
    parser.add_argument('command', choices=['status', 'sdtest', 'mount', 'prepare-fresh-sd', 'screen', 'clock'])
    args = parser.parse_args()
    commands = {'status': 'STATUS', 'sdtest': 'SDTEST', 'mount': 'MOUNT_SD',
                'prepare-fresh-sd': 'PREPARE_FRESH_SD', 'screen': 'SCREEN', 'clock': f'CLOCK {int(time.time())}'}
    port = serial.Serial(port=None, baudrate=115200, timeout=0.3)
    port.dtr = False
    port.rts = False
    port.port = args.port
    port.open()
    try:
        port.reset_input_buffer()
        port.write((commands[args.command] + '\n').encode())
        deadline = time.monotonic() + 90
        recent = []
        while time.monotonic() < deadline:
            line = port.readline()
            if not line:
                continue
            text = line.decode('utf-8', errors='replace').strip()
            recent.append(text)
            if text.startswith('RIG_FRAME '):
                _, width, height, length = text.split()
                data = bytearray()
                while len(data) < int(length) and time.monotonic() < deadline:
                    data.extend(port.read(int(length) - len(data)))
                if len(data) != int(length):
                    raise RuntimeError(f'Incomplete frame: {len(data)}/{length} bytes')
                target = args.output or Path('lcd-screen.ppm')
                target.write_bytes(f'P6\n{width} {height}\n255\n'.encode() + data)
                print(f'Framebuffer saved: {target.resolve()} ({width}x{height})')
                return
            if text.startswith('RIG_STATUS '):
                status = json.loads(text[len('RIG_STATUS '):])
                if args.output:
                    args.output.write_text(json.dumps(status, indent=2) + '\n', encoding='utf-8')
                print(json.dumps(status, indent=2))
                return
            if text.startswith('RIG_SD '):
                print(text)
                if ' ERROR ' in text:
                    raise RuntimeError(text)
            if text.startswith('RIG_CLOCK '):
                print(text)
                return
        raise RuntimeError('No service response. Recent output:\n' + '\n'.join(recent[-20:]))
    finally:
        port.close()


if __name__ == '__main__':
    main()
