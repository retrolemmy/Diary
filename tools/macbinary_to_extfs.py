#!/usr/bin/env python3
"""Unpacks a MacBinary file into a folder shared with Basilisk II (ExtFS).

Basilisk II shows a Linux folder as a Mac volume and keeps what Linux
files can't hold in helper files next to them:
    folder/NAME          data fork
    folder/.rsrc/NAME    resource fork
    folder/.finf/NAME    FInfo + FXInfo (16 + 16 bytes): type, creator, flags
Usage: macbinary_to_extfs.py file.bin folder
"""
import os
import struct
import sys


def main():
    source, folder = sys.argv[1], sys.argv[2]
    data = open(source, 'rb').read()
    header = data[:128]
    name = header[2:2 + header[1]].decode('mac_roman')
    data_len, rsrc_len = struct.unpack('>II', header[83:91])
    data_start = 128
    rsrc_start = data_start + (data_len + 127) // 128 * 128

    finder_flags = header[73] << 8 | header[101]
    finf = (header[65:73]                           # fdType, fdCreator
            + struct.pack('>HhhH', finder_flags,
                          -1, -1,                   # fdLocation: let the Finder place it
                          0)                        # fdFldr
            + bytes(16))                            # FXInfo

    for sub in ('.rsrc', '.finf'):
        os.makedirs(os.path.join(folder, sub), exist_ok=True)
    with open(os.path.join(folder, name), 'wb') as f:
        f.write(data[data_start:data_start + data_len])
    with open(os.path.join(folder, '.rsrc', name), 'wb') as f:
        f.write(data[rsrc_start:rsrc_start + rsrc_len])
    with open(os.path.join(folder, '.finf', name), 'wb') as f:
        f.write(finf)
    print(f'{name}: {data_len} + {rsrc_len} bytes, '
          f'{header[65:69].decode("mac_roman")}/{header[69:73].decode("mac_roman")}, '
          f'flags {finder_flags:#06x} -> {folder}')


if __name__ == '__main__':
    main()
