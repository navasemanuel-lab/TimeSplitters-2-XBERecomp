"""Docks (TimeSplitters 1's level 8, PS2) as an arcade map of TimeSplitters 2, level number 54 (arcade/l_54_AR.pak,
hidden_levels.cpp), built the way tools/mk_chemical_plant.py builds Chemical Plant.

python tools/mk_docks.py <TS1 PAK dir (BG.PAK, MISC.PAK)> <TSPC assets_pc folder> <retail arcade/l_35_AR.pak> <retail arcade/l_21_AR.pak> <out l_54_AR.pak>

The geometry is converted from TS1's PS2 level8.raw (ts1_to_xbr.py); the pads, the room names and the ambient of the map are the
PC build's (xbob.pak has level8.h and pad/data/level8.raw, no geometry); the textures come from TS1's MISC.PAK (.qpm) as new ids
(31300 +); the base is the retail Dam arcade pak (the models, sounds and music), as for Chemical Plant, with its sky."""
import glob, os, struct, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, r'C:\TS2-XBE\ts2_demo_analysis\tools')
import numpy as np
import pak as PK
import ps2conv as C
import ts1mdl as T
import ts1_to_xbr
from pakedit import edit_pak, read_pak

NUMBER = 54
FIRST_TEXTURE = 31300

ts1_dir, tspc, dam_path, l21_path, out = sys.argv[1:6]
bg = os.path.join(ts1_dir, 'BG.PAK'); misc = os.path.join(ts1_dir, 'MISC.PAK')


def pak_file(path, name):
    for n, o, s, e in PK.entries(path):
        if n == name:
            with open(path, 'rb') as f:
                f.seek(o); return f.read(s)
    raise KeyError(name)


level = pak_file(bg, 'bg/level8/level8.raw')
xbr, tex_ids, stats = ts1_to_xbr.build(level, FIRST_TEXTURE)
print('level:', stats)

X = read_pak(open(os.path.join(tspc, 'data', 'xbob.pak'), 'rb').read())
h = X['bg/level8/level8.h'].replace(b'LEVEL8_', b'LEVEL%d_' % NUMBER)
rooms = X['bg/level8/level8.h'].count(b'ROOMNUM')

d = bytearray(open(dam_path, 'rb').read())
mg, do, n, no, ns = struct.unpack_from('<4sIIII', d, 0)
assert mg == b'P8CK'
d[no:no + ns] = bytes(d[no:no + ns]).replace(b'level35', b'level%d' % NUMBER)
for i in range(n):
    a, size, off = struct.unpack_from('<III', d, do + i * 12)
    e = d.index(b'\0', no + a)
    if bytes(d[no + a:e]).decode().endswith(('level%d.h' % NUMBER, 'level%d_ambient.s' % NUMBER)):
        d[off:off + size] = bytes(d[off:off + size]).replace(b'L35_', b'L%d_' % NUMBER)
base = bytes(d)
dam = read_pak(base)
l21 = read_pak(open(l21_path, 'rb').read())



def pads_v2(b):
    """the PC build's Docks pads are a version 1 file: {1, pads, links, 7 words a pad, 5 a link}; the game reads version 2: pads of
    8 words (the last one 2, as 400 of Chemical Plant's 403 have it), links whose first word is 0x10000 (1 there), and the count
    of the external references (none) at the end"""
    w = struct.unpack('<%dI' % (len(b) // 4), b)
    assert w[0] == 1
    n1, n2 = w[1], w[2]
    a = 3 + 7 * n1
    assert a + 5 * n2 == len(w)
    out = [2, n1, n2]
    for i in range(n1): out += list(w[3 + 7 * i:10 + 7 * i]) + [2]
    for i in range(n2):
        l = list(w[a + 5 * i:a + 5 * i + 5]); l[0] = 0x10000 if l[0] == 1 else l[0]; out += l
    out.append(0)
    return struct.pack('<%dI' % len(out), *out)


PADS = pads_v2(X['pad/data/level8.raw'])
if len(sys.argv) > 6: open(sys.argv[6], 'wb').write(PADS)
files = {'bg/level%d/level%d.xbr' % (NUMBER, NUMBER): xbr,
         'bg/level%d/level%d.h' % (NUMBER, NUMBER): h,
         'pad/data/level%d.raw' % NUMBER: PADS}
files['bg/ambient/level%d_ambient.raw' % NUMBER] = struct.pack('<f', 0.6) * 3 * (rooms + 3)
files['bg/ambient/level%d_ambient.s' % NUMBER] = (b'#Room Name, Red, Green, Blue\n\n' + b''.join(
    b'LEVEL%d_ROOMNUM_%s, 0.600000, 0.600000, 0.600000\n' % (NUMBER, l.split()[1][len(b'LEVEL8_ROOMNUM_'):]) for l in X['bg/level8/level8.h'].split(b'\n')
    if l.startswith(b'#define LEVEL8_ROOMNUM_')))

# the textures: TS1's, converted
MI = {n: (o, s) for n, o, s, e in PK.entries(misc)}
with open(misc, 'rb') as f:
    for i, t in enumerate(tex_ids):
        o, s = MI['textures/%04d.qpm' % t]
        f.seek(o)
        img = T.qpm_rgba(f.read(s))
        files['textures/%04d.xbt' % (FIRST_TEXTURE + i)] = C.xbox_texture(img, True)
print('textures converted:', len(tex_ids))

sky = l21['ob/skies/skycoldnight.xbr']
files['ob/skies/skycoldnight.xbr'] = sky
o = struct.unpack_from('<I', sky, 0)[0]; sky_ids = []
while struct.unpack_from('<I', sky, o)[0] != 0xFFFFFFFF:
    sky_ids.append(struct.unpack_from('<I', sky, o)[0]); o += 16
for t in sky_ids:
    k = 'textures/%04d.xbt' % t
    if k not in dam and k in l21: files[k] = l21[k]
new = edit_pak(base, files)
open(out, 'wb').write(new)
print('written', out, len(new), 'bytes,', len(files), 'files')
