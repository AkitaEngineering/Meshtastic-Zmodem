#!/usr/bin/env python3
"""
Simple PTY <-> real-serial proxy used to attach host XMODEM/SZ/SX tools
to a real device. The script creates a virtual PTY (slave) and prints its
path; run your host transfer tool against that PTY while this script
forwards bytes to/from the real serial device.

Usage:
  python3 tools/serial_proxy.py --dev /dev/ttyUSB0 --baud 115200

Dependencies: pyserial
"""
import os
import pty
import argparse
import threading
import sys
import time

try:
    import serial
except ImportError:
    print('pyserial required: pip install pyserial')
    sys.exit(1)


def forward_serial_to_pty(ser, master_fd):
    try:
        while True:
            data = ser.read(1024)
            if data:
                os.write(master_fd, data)
            else:
                time.sleep(0.01)
    except Exception:
        pass


def forward_pty_to_serial(ser, master_fd):
    try:
        while True:
            try:
                data = os.read(master_fd, 1024)
            except OSError:
                break
            if data:
                ser.write(data)
            else:
                time.sleep(0.01)
    except Exception:
        pass


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--dev', required=True, help='Real serial device (e.g. /dev/ttyUSB0)')
    p.add_argument('--baud', type=int, default=115200, help='Baud rate for real device')
    args = p.parse_args()

    if not os.path.exists(args.dev):
        print('Device not found:', args.dev)
        sys.exit(1)

    master_fd, slave_fd = pty.openpty()
    slave_name = os.ttyname(slave_fd)
    print('PTY slave device created for host tool:', slave_name)
    print('Run your host-side tool (e.g. sz/rx) against that path.')

    ser = serial.Serial(args.dev, args.baud, timeout=0)

    t1 = threading.Thread(target=forward_serial_to_pty, args=(ser, master_fd), daemon=True)
    t2 = threading.Thread(target=forward_pty_to_serial, args=(ser, master_fd), daemon=True)
    t1.start(); t2.start()

    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print('Exiting...')
    finally:
        try:
            ser.close()
        except Exception:
            pass


if __name__ == '__main__':
    main()
