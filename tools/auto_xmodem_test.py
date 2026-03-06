#!/usr/bin/env python3
"""
Automated XMODEM test harness.
Creates a PTY slave, launches `sz --xmodem` to send a test file to the slave,
and acts as a CRC-mode XMODEM receiver on the master side to validate data.

Requirements: `sz` from lrzsz installed on the system.
"""
import os
import pty
import subprocess
import tempfile
import time
import sys
import argparse

# XMODEM constants
XSOH = 0x01
XSTX = 0x02
XEOT = 0x04
XACK = 0x06
XNAK = 0x15
XCAN = 0x18


def updcrc(c, crc):
    crc = crc ^ (c << 8)
    for _ in range(8):
        if crc & 0x8000:
            crc = ((crc << 1) & 0xFFFF) ^ 0x1021
        else:
            crc = (crc << 1) & 0xFFFF
    return crc


def calc_crc16(data):
    crc = 0
    for b in data:
        crc = updcrc(b, crc)
    return crc & 0xFFFF


def run_test(sz_path='sz'):
    # create a temporary file with random data
    with tempfile.NamedTemporaryFile(delete=False) as tf:
        tf.write(os.urandom(1024))
        tf.flush()
        src_path = tf.name

    master_fd, slave_fd = pty.openpty()
    slave_name = os.ttyname(slave_fd)
    print('PTY slave:', slave_name)

    # Launch sz to send the file, wired to slave
    # sz reads/writes the slave device as its stdin/stdout
    sz_proc = subprocess.Popen([sz_path, '--xmodem', src_path], stdin=open(slave_name, 'rb'), stdout=open(slave_name, 'wb'), stderr=subprocess.PIPE)

    # Act as XMODEM CRC receiver on master_fd
    received = bytearray()
    expected_block = 1
    # send initial 'C' to request CRC-mode
    os.write(master_fd, b'C')

    start = time.time()
    try:
        while True:
            # read 1 byte min
            try:
                b = os.read(master_fd, 1)
            except OSError:
                b = b''
            if not b:
                # check for process exit
                if sz_proc.poll() is not None:
                    break
                if time.time() - start > 30:
                    print('Timeout waiting for data')
                    break
                time.sleep(0.01)
                continue

            ch = b[0]
            if ch == XEOT:
                # consume and ack
                os.write(master_fd, bytes([XACK]))
                print('Received EOT')
                break
            if ch not in (XSOH, XSTX):
                # ignore other bytes
                continue

            block_size = 128 if ch == XSOH else 1024
            # read block header
            hdr = os.read(master_fd, 2)
            if len(hdr) < 2:
                print('Incomplete header')
                break
            blk = hdr[0]
            blkcomp = hdr[1]
            if (blk ^ blkcomp) != 0xFF:
                os.write(master_fd, bytes([XNAK])); continue

            # read data and CRC
            data = b''
            while len(data) < block_size:
                data += os.read(master_fd, block_size - len(data))
            crc_bytes = os.read(master_fd, 2)
            if len(crc_bytes) < 2:
                print('Missing CRC')
                os.write(master_fd, bytes([XNAK])); continue
            rec_crc = (crc_bytes[0] << 8) | crc_bytes[1]
            calc = calc_crc16(data)
            if calc != rec_crc:
                print('CRC mismatch for block', expected_block)
                os.write(master_fd, bytes([XNAK]));
                continue
            # block OK
            received += data
            os.write(master_fd, bytes([XACK]))
            expected_block = (expected_block + 1) & 0xFF

    finally:
        try:
            sz_proc.terminate()
        except Exception:
            pass

    # compare first len(src) bytes
    with open(src_path, 'rb') as f:
        orig = f.read()
    # trim padding (0x1A) from received end up to len(orig)
    rec_trim = received[:len(orig)]
    ok = rec_trim == orig
    print('Result:', 'PASS' if ok else 'FAIL')
    if not ok:
        # write files for inspection
        with open(src_path + '.recv', 'wb') as rf:
            rf.write(rec_trim)
        print('Wrote', src_path + '.recv')
    # cleanup
    try:
        os.unlink(src_path)
    except Exception:
        pass
    return ok


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--sz', default='sz', help='Path to sz (lrzsz)')
    args = parser.parse_args()
    ok = run_test(args.sz)
    sys.exit(0 if ok else 2)
