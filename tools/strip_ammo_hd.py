"""Takes the redrawn ammunition icons (32x32 textures replaced by 256x256 pictures) out of an XMS1 pack, so the game's own
(TS2 Redux's, unchanged) icons show: python -I tools/strip_ammo_hd.py <in.bin> <out.bin>"""
import struct, sys
d = open(sys.argv[1], 'rb').read()
mg, n = struct.unpack_from('<4sI', d, 0)
assert mg == b'XMS1'
F = '<IIIQIIQ'; S = struct.calcsize(F)
ents = [struct.unpack_from(F, d, 8 + i * S) for i in range(n)]
keep = [e for e in ents if not (e[0] == 32 and e[1] == 32 and e[4] == 256 and e[5] == 256)]
print('entries', n, '->', len(keep))
hs = 8 + len(keep) * S
blobs = []; where = {}; off = 0
out = []
for w, h, nb, hs_, sw, sh, o in keep:
    if o not in where:
        where[o] = off; b = d[o:o + sw * sh * 4]; blobs.append(b); off += len(b)
    out.append(struct.pack(F, w, h, nb, hs_, sw, sh, where[o] + hs))
open(sys.argv[2], 'wb').write(struct.pack('<4sI', b'XMS1', len(keep)) + b''.join(out) + b''.join(blobs))
print('bytes', hs + off)
