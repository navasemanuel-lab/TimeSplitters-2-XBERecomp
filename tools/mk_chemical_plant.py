"""Chemical Plant for the arcade: the real TimeSplitters level (level 5 of TimeSplitters 2 PC's xbob.pak), under level number 53
(arcade/l_53_AR.pak, the number the game's list code uses, hidden_levels.cpp).

python -I tools/mk_chemical_plant.py <retail arcade/l_35_AR.pak> <retail arcade/l_21_AR.pak> <TSPC assets_pc folder> <out l_53_AR.pak>

The base is the retail Dam arcade pak (its models, sounds and music: Chemical Plant's setup only places the guns, ammo and
bags of the shared object set). The level files are named by the level number (bg/level53/level53.xbr, bg/ambient/level53_ambient.*,
pad/data/level53.raw, and the .h names LEVEL53_...). Replaced here:
  bg/level53/level53.xbr        TSPC's level5.xbr (Xbox format as it is: TSPC's Dam xbr is byte for byte the Xbox one)
  bg/level53/level53.h          level5.h with LEVEL5_ -> LEVEL53_
  pad/data/level53.raw          level5.raw (its pad ids are the ones the PC build's setup for the map uses)
  bg/ambient/level53_ambient.*  flat 0.6 grey in every room (level 5 has no ambient file)
  textures/NNNN.xbt             the level's textures: from the Xbox paks when one has the id, else converted from TSPC's textures/NNNN.rgb
  ob/skies/skycoldnight.xbr     the sky the PC build gives the map (sky index 5, as Dam's neighbour level 21 uses), with its textures
"""
import glob, os, struct, sys
sys.path.insert(0, r'C:\TS2-XBE\ts2_demo_analysis\tools')
import numpy as np
from PIL import Image
import ps2conv as C
from pakedit import edit_pak, read_pak

dam_path, l21_path, tspc, out = sys.argv[1:5]
d = bytearray(open(dam_path, 'rb').read())
# the same-length renames of the Dam pak's names and of its .h/.s text
mg, do, n, no, ns = struct.unpack_from('<4sIIII', d, 0)
assert mg == b'P8CK'
d[no:no + ns] = bytes(d[no:no + ns]).replace(b'level35', b'level53')
for i in range(n):
    a, size, off = struct.unpack_from('<III', d, do + i * 12)
    e = d.index(b'\0', no + a)
    if bytes(d[no + a:e]).decode().endswith(('level53.h', 'level53_ambient.s')):
        d[off:off + size] = bytes(d[off:off + size]).replace(b'L35_', b'L53_')
base = bytes(d)
dam = read_pak(base)
l21 = read_pak(open(l21_path, 'rb').read())

xb = open(os.path.join(tspc, 'data', 'xbob.pak'), 'rb').read()
X = read_pak(xb)
level = X['bg/level5/level5.xbr']
files = {'bg/level53/level53.xbr': level,
         'bg/level53/level53.h': X['bg/level5/level5.h'].replace(b'LEVEL5_', b'LEVEL53_'),
         'pad/data/level53.raw': X['pad/data/level5.raw']}
rooms = X['bg/level5/level5.h'].count(b'ROOMNUM')
files['bg/ambient/level53_ambient.raw'] = struct.pack('<f', 0.6) * 3 * (rooms + 3)
files['bg/ambient/level53_ambient.s'] = (b'#Room Name, Red, Green, Blue\n\n' + b''.join(
    b'LEVEL53_ROOMNUM_%s, 0.600000, 0.600000, 0.600000\n' % l.split()[1][len(b'LEVEL5_ROOMNUM_'):] for l in X['bg/level5/level5.h'].split(b'\n')
    if l.startswith(b'#define LEVEL5_ROOMNUM_')))

def index(path):
    # name -> (offset, size) of a pak, without reading its files
    f = open(path, 'rb')
    if f.read(4) != b'P8CK': return {}
    tab, n, names = struct.unpack('<3I', f.read(12))
    f.seek(tab); t = f.read(12 * n); f.seek(names); nm = f.read(1 << 22)
    out = {}
    for i in range(n):
        no, sz, off = struct.unpack_from('<3I', t, 12 * i)
        out[nm[no:nm.index(bytes(1), no)].decode()] = (off, sz)
    return out


xbox_paks = []
for p in glob.glob(r'C:\TS2-XBE\data\**\*.pak', recursive=True):
    ix = index(p)
    if ix: xbox_paks.append((p, ix))


def xbox_file(k):
    for p, ix in xbox_paks:
        if k in ix:
            with open(p, 'rb') as f:
                f.seek(ix[k][0]); return f.read(ix[k][1])


def tex_ids(xbr):
    o = struct.unpack_from('<I', xbr, 0)[0]; ids = []
    while struct.unpack_from('<I', xbr, o)[0] != 0xFFFFFFFF:
        ids.append(struct.unpack_from('<I', xbr, o)[0]); o += 16
    return ids


stats = {'dam': 0, 'xbox': 0, 'converted': 0}


def same_shape(b, t):
    # the Xbox texture of the id is the level's own only when it has the shape of TSPC's picture of that id (a few ids are
    # another picture there, turned a quarter: their coordinates would be wrong on it)
    p = os.path.join(tspc, 'textures', '%d.rgb' % t)
    if not os.path.exists(p): return True
    w, h = struct.unpack_from('<II', b, 0)
    return Image.open(p).size == (w, h)


def add_texture(t, sources):
    k = 'textures/%04d.xbt' % t
    if k in files:
        stats['dam'] += 1; return
    if k in dam and (sources is not None or same_shape(dam[k], t)):
        stats['dam'] += 1; return
    if sources is None:
        b = xbox_file(k)
        if b is not None and same_shape(b, t):
            files[k] = b; stats['xbox'] += 1; return
    else:
        for P in sources:
            if k in P:
                files[k] = P[k]; stats['xbox'] += 1; return
    im = Image.open(os.path.join(tspc, 'textures', '%d.rgb' % t)).convert('RGBA')
    files[k] = C.xbox_texture(np.asarray(im), True); stats['converted'] += 1


for t in tex_ids(level):
    add_texture(t, None)
sky = l21['ob/skies/skycoldnight.xbr']
files['ob/skies/skycoldnight.xbr'] = sky
for t in tex_ids(sky):
    add_texture(t, [l21])
print('level textures %d: kept from Dam %d, from Xbox paks %d, converted from TSPC %d' % (len(tex_ids(level)), stats['dam'], stats['xbox'], stats['converted']))
new = edit_pak(base, files)
open(out, 'wb').write(new)
print('written', out, len(new), 'bytes,', len(files), 'files')
