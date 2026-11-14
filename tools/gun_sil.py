# TS2 gun.pak models -> side-view silhouette masks (PNG) for the weapon bar icons.
#   python -I gun_sil.py GUN_PAK ../tools/_sil [gun ...]
import sys, os, struct
here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, r'C:\Users\navas\AppData\Local\Temp\claude\C--TSFP-XBE\a4e9725c-8c24-4ac0-8a33-60f1fa005d73\scratchpad\sg')
sys.path.insert(0, r'C:\Users\navas\AppData\Local\Temp\claude\C--TSFP-XBE\a4e9725c-8c24-4ac0-8a33-60f1fa005d73\scratchpad\sg\lib')
import ts2geo
from PIL import Image, ImageDraw
def pak(path):
    d = open(path, 'rb').read(); tab, n, names = struct.unpack_from('<3I', d, 4); o = {}
    for i in range(n):
        no, sz, off = struct.unpack_from('<3I', d, tab + 12 * i)
        o[d[names + no:d.index(b'\0', names + no)].decode()] = d[off:off + sz]
    return o
def tris(data, sections=(0,), drop=None):
    x, geo = ts2geo.geometry(data); out = []
    for i, s, vs, ts in geo:
        if s not in sections: continue
        for t in ts: out.append([vs[q][0] for q in t[:3]])
    return out
def mask(T, axes=(2, 1), W=1600, pad=24):
    pts = [(p[axes[0]], p[axes[1]]) for t in T for p in t]
    x0 = min(p[0] for p in pts); x1 = max(p[0] for p in pts); y0 = min(p[1] for p in pts); y1 = max(p[1] for p in pts)
    sc = (W - 2 * pad) / max(x1 - x0, 1e-6); H = int((y1 - y0) * sc) + 2 * pad
    im = Image.new('L', (W, H), 0); d = ImageDraw.Draw(im)
    for t in T:
        d.polygon([(pad + (p[axes[0]] - x0) * sc, H - pad - (p[axes[1]] - y0) * sc) for p in t], fill=255)
    return im
if __name__ == '__main__':
    P = pak(sys.argv[1]); out = sys.argv[2]; os.makedirs(out, exist_ok=True)
    names = sys.argv[3:] or sorted(k[8:-7] for k in P if k.endswith('_cl.xbr'))
    for g in names:
        for suf in ('_cl', '_ph'):
            k = 'ob/guns/%s%s.xbr' % (g, suf)
            if k not in P: continue
            try: T = tris(P[k])
            except Exception as e: print(k, 'ERR', e); continue
            if not T: print(k, 'empty'); continue
            mask(T).save(os.path.join(out, g + suf + '.png')); print(k, len(T))
