"""The promo models of gun.pak (ob/guns/*_promo.xbr, whole guns in side view, as the gun sheet shows them) -> side silhouette
masks (PNG), for the weapon bar's icons: python -I tools/promo_sil.py <gun.pak> <out dir> [gun ...]
Uses the Xbox model decoder of ts2_demo_analysis/tools (mdl.py, mdl_tris.py): the first pass of every node, the model's z
along the gun, y up."""
import os, struct, sys
sys.path.insert(0, r'C:\TS2-XBE\ts2_demo_analysis\tools')
from mdl import Model
import mdl_tris
from PIL import Image, ImageDraw


def pak(path):
    d = open(path, 'rb').read(); tab, n, names = struct.unpack_from('<3I', d, 4); o = {}
    for i in range(n):
        no, sz, off = struct.unpack_from('<3I', d, tab + 12 * i)
        o[d[names + no:d.index(b'\0', names + no)].decode()] = d[off:off + sz]
    return o


def triangles(data, passes=(0,)):
    m = Model(data, False); out = []
    for i in range(m.n):
        for k, (P, T, ch) in enumerate(mdl_tris.xbox_node(m, i)):
            if k not in passes: continue
            for t in T:
                if max(t) < len(P): out.append([P[j][:3] for j in t])
    return out


def mask(T, W=1600, pad=24, axes=(2, 1)):
    pts = [(p[axes[0]], p[axes[1]]) for t in T for p in t]
    x0 = min(p[0] for p in pts); x1 = max(p[0] for p in pts); y0 = min(p[1] for p in pts); y1 = max(p[1] for p in pts)
    sc = (W - 2 * pad) / max(x1 - x0, 1e-6); H = int((y1 - y0) * sc) + 2 * pad
    im = Image.new('L', (W, H), 0); d = ImageDraw.Draw(im)
    for t in T:
        d.polygon([(pad + (p[axes[0]] - x0) * sc, H - pad - (p[axes[1]] - y0) * sc) for p in t], fill=255)
    return im


if __name__ == '__main__':
    P = pak(sys.argv[1]); out = sys.argv[2]; os.makedirs(out, exist_ok=True)
    names = sys.argv[3:] or sorted(k[len('ob/guns/'):-len('_promo.xbr')] for k in P if k.endswith('_promo.xbr'))
    for g in names:
        k = 'ob/guns/%s_promo.xbr' % g
        try: T = triangles(P[k])
        except Exception as e: print(k, 'ERR', e); continue
        if not T: print(k, 'empty'); continue
        mask(T).save(os.path.join(out, g + '.png')); print(k, len(T))
