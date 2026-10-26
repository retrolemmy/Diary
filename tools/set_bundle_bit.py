#!/usr/bin/env python3
"""Sets the Finder's hasBundle flag in a MacBinary file, in place.

Retro68's Rez can't set Finder flags, and without hasBundle the Finder
ignores the application's BNDL and shows a generic icon.
Usage: set_bundle_bit.py file.bin
"""
import sys

HAS_BUNDLE = 0x20   # high byte of fdFlags (0x2000)


def crc16_xmodem(data):
    crc = 0
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else crc << 1
            crc &= 0xFFFF
    return crc


path = sys.argv[1]
with open(path, 'r+b') as f:
    header = bytearray(f.read(128))
    header[73] |= HAS_BUNDLE
    header[124:126] = crc16_xmodem(header[:124]).to_bytes(2, 'big')
    f.seek(0)
    f.write(header)
