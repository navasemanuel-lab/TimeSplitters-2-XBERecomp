"""Dump the Xbox weapon table (0x2C9BF0, 0x24 B an entry) and gun table (0x2C3468, 0x240 B a gun) of default.xbe.
python -I tools/dump_guns.py assets/default.xbe"""
import struct, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from xbe_re import Xbe

x = Xbe(sys.argv[1])


def cstr(va):
    b = x.read(va, 48) if 0x10000 < va < 0x1000000 else None
    if not b:
        return None
    s = b.split(b'\0')[0]
    return s.decode('latin1') if s and all(32 <= c < 127 for c in s) else None


print('weapon table')
for i in range(48):
    b = x.read(0x2C9BF0 + i * 0x24, 0x24)
    w = struct.unpack('<9I', b)
    print(i, ' '.join('%X' % v for v in w), [cstr(v) for v in w if cstr(v)])
print('gun table')
for g in range(40):
    b = x.read(0x2C3468 + g * 0x240, 0x240)
    w = struct.unpack('<144I', b)
    names = [(k, cstr(v)) for k, v in enumerate(w) if cstr(v)]
    print(g, 'ammo', w[4], names[:6])
