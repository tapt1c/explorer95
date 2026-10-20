"""Normalize Open Watcom LE metadata for the standalone native probe."""
import struct
import sys
from pathlib import Path

p = Path(sys.argv[1] if len(sys.argv)>1 else 'EX95KEY0.VXD')
b = bytearray(p.read_bytes())
h = struct.unpack_from('<I', b, 60)[0]
assert b[h:h+2] == b'LE'
o, n = struct.unpack_from('<II', b, h+0x40)
for i in range(n):
    at = h + o + 24*i
    flags = struct.unpack_from('<I', b, at+8)[0]
    # VxD objects share a flat address space and are executable.
    struct.pack_into('<II', b, at+4, 0, (flags | 4) & ~0x1300)
entry = h + struct.unpack_from('<I', b, h+0x5c)[0]
assert b[entry:entry+2] == bytes([1, 3])
b[entry+4] |= 2  # DDB entry is shared as well as exported.
struct.pack_into('<HH', b, 2, len(b)%512, (len(b)+511)//512)
p.write_bytes(b)
