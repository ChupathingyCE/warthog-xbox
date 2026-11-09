#!/usr/bin/env python3
"""lld fills a code section's file bytes with 0xCC, and the sdk libraries keep uninitialized data
(*_URW: D3D_URW, DSOUND_URW...) inside code sections, so what must start as zero starts as
0xCCCCCCCC as far as the section's last file block reaches (CDirectSound::m_pDirectSound: a crash in
DirectSoundCreate at boot). zeroes those bytes in the linked exe, before cxbe. usage: zero_urw.py halo.exe halo.map"""
import re, struct, sys

exe, map_file = sys.argv[1:3]
data = bytearray(open(exe, 'rb').read())
pe = struct.unpack_from('<I', data, 0x3c)[0]
count = struct.unpack_from('<H', data, pe + 6)[0]
optional = struct.unpack_from('<H', data, pe + 20)[0]
table = pe + 24 + optional
sections = []
for i in range(count):
    name = bytes(data[table + i * 40:table + i * 40 + 8]).rstrip(b'\0').decode()
    virtual_size, va, raw_size, raw = struct.unpack_from('<IIII', data, table + i * 40 + 8)
    sections.append((name, va, raw_size, raw))
total = 0
for line in open(map_file, errors='replace'):
    m = re.match(r'^\s*([0-9a-fA-F]{4}):([0-9a-fA-F]{8})\s+([0-9a-fA-F]{8})H\s+(\S+_URW)\s', line)
    if not m:
        continue
    name, va, raw_size, raw = sections[int(m.group(1), 16) - 1]
    offset, length = int(m.group(2), 16), int(m.group(3), 16)
    end = min(offset + length, raw_size)
    if end <= offset:
        print(f'{m.group(4)}: {length:#x} bytes, past the file: already zero')
        continue
    before = sum(b == 0xCC for b in data[raw + offset:raw + end])
    data[raw + offset:raw + end] = bytes(end - offset)
    total += end - offset
    print(f'{m.group(4)} in {name}: {end - offset:#x} of {length:#x} bytes zeroed ({before:#x} were 0xCC)')
open(exe, 'wb').write(data)
print(f'{total:#x} bytes')
