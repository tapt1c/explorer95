"""Inert negative loader control; not a working driver or application backend."""
import struct
from pathlib import Path

root=Path.cwd()
mz=bytearray((root/'EX95MIN0.VXD').read_bytes()[:128])
le=bytearray(196)
def word(at,value): struct.pack_into('<H',le,at,value)
def dword(at,value): struct.pack_into('<I',le,at,value)
le[:2]=b'LE'
word(8,2); word(10,4)
dword(0x10,0x38000); dword(0x14,1)
dword(0x28,512); dword(0x2c,100)
dword(0x30,16); dword(0x38,50)
dword(0x40,196); dword(0x44,1); dword(0x48,220)
dword(0x58,224); dword(0x5c,236)
dword(0x68,246); dword(0x6c,254)
dword(0x70,261); dword(0x78,261)
dword(0x80,4096); dword(0x84,1)
word(0xc2,0x400)
obj=struct.pack('<6I',100,0,0x2045,1,1,0x444f434c)
page=bytes([0,0,1,0])
names=bytes([8])+b'EX95MIN1'+bytes(3)
entry=bytes([1,3,1,0,3])+struct.pack('<I',16)+bytes(1)
fixups=struct.pack('<II',0,7)+bytes([7,0,40,0,1,96,0])+bytes(1)
head=mz+le+obj+page+names+entry+fixups
assert len(head)==390
ddb=struct.pack('<IHHBBH8s15I',0,0x400,0,0,1,0,b'EX95MIN1',
                0x80000000,96,0,0,0,0,0,0,0,0,0,80,0,0,0)
body=bytes(16)+ddb+bytes.fromhex('33 c0 f8 c3')
assert len(body)==100
out=head+bytes(4096-len(head))+body
struct.pack_into('<HH',out,2,len(out)%512,(len(out)+511)//512)
(root/'EX95MIN1.VXD').write_bytes(out)
