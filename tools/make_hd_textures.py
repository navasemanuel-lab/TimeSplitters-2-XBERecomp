"""Builds data/hd_textures.bin, the front end's header pictures and logo at TS2 Redux's resolution, from its pak and the
game's own front end pak: python -I tools/make_hd_textures.py [ts2_redux.pak] [l_101.pak] [out.bin]

Redux's pictures are SGI images kept upside down. The headers (ts2banner1-4, 1024x256) have the logo drawn in and
Redux's name under it; the name is painted out from the picture around it. The logo (ts2newlogo, 1024x512) loses the
name, gets a dark glow around it and is laid where the console's texture has it, for every draw of that texture. The small banners are taken as they are.

Each picture replaces the console texture whose level 0 hashes as the renderer hashes it (gpu::Hash):
"HDT1", count, then for each: name (32 bytes), console width, height, bytes hashed, hash (u64), width, height,
then its pixels as BGRA."""
import io
import os
import struct
import sys
import zipfile

import cv2
import numpy as np
from PIL import Image, ImageFilter

REDUX = r'C:\Program Files (x86)\Steam\steamapps\common\Homefront_The_Revolution\gamehf2\ts2_redux.pak'
redux_path = sys.argv[1] if len(sys.argv) > 1 else REDUX
xbox_path = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(__file__), '..', 'assets', 'data', 'arcade', 'l_101.pak')
out_path = sys.argv[3] if len(sys.argv) > 3 else os.path.join(os.path.dirname(__file__), '..', 'data', 'hd_textures.bin')

M64 = (1 << 64) - 1
K0, K1 = 0x9E3779B97F4A7C15, 0xE7037ED1A0B428DB


def mix(a, b):
    r = (a & M64) * (b & M64)
    return (r ^ (r >> 64)) & M64


def gpu_hash(b, h=1469598103934665603):
    """gpu::Hash (runtime/gpu/shaders.cpp)"""
    n = len(b)
    seed = h ^ K0
    i = 0
    while i + 16 <= n:
        x, y = struct.unpack_from('<QQ', b, i)
        seed = mix(x ^ K1, y ^ seed)
        i += 16
    r = n - i
    if r:
        if r >= 8:
            x = struct.unpack_from('<Q', b, i)[0]
            y = struct.unpack_from('<Q', b, n - 8)[0]
        else:
            x = int.from_bytes(b[i:n], 'little')
            y = 0
        seed = mix(x ^ K1, y ^ seed ^ r)
    return mix(seed ^ K0, n ^ K1)


def xbox_textures(path):
    """the pak's textures/misc/*.xbt: name -> (width, height, level 0 bytes); format 0 DXT1, 1 A8R8G8B8"""
    d = open(path, 'rb').read()
    assert d[:4] == b'P8CK'
    directory, count, names = struct.unpack_from('<3I', d, 4)
    out = {}
    for i in range(count):
        name_at, size, offset = struct.unpack_from('<3I', d, directory + i * 12)
        name = d[names + name_at:d.index(b'\0', names + name_at)].decode()
        if not name.startswith('textures/misc/'):
            continue
        w, h, _, _, fmt = struct.unpack_from('<5I', d, offset)
        # 0 DXT1, 1 A8R8G8B8, 4..8 compressed with alpha (one byte a texel)
        level0 = w * h // 2 if fmt == 0 else w * h * 4 if fmt == 1 else w * h
        out[name[14:-4]] = (w, h, d[offset + 128:offset + 128 + level0])
    return out


def redux_picture(z, name):
    return Image.open(io.BytesIO(z.read('ts2/textures/misc/%s.rgb' % name))).convert('RGBA').transpose(Image.FLIP_TOP_BOTTOM)


def name_mask(rgba, box):
    """Redux's name in box (x0, y0, x1, y1): its green letters, grown over their dark edge and glow"""
    a = np.asarray(rgba).astype(int)
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    green = (g > r + 40) & (g > b + 25)
    m = np.zeros(green.shape, np.uint8)
    x0, y0, x1, y1 = box
    m[y0:y1, x0:x1] = green[y0:y1, x0:x1] * 255
    return cv2.dilate(m, np.ones((3, 3), np.uint8), iterations=7)


def banner(z, name):
    im = redux_picture(z, name)
    w, h = im.size
    # the name sits under the logo, left of the "2"'s tail
    m = name_mask(im, (int(w * 0.33), int(h * 0.6), int(w * 0.5), h))
    rgb = cv2.cvtColor(np.asarray(im)[..., :3], cv2.COLOR_RGB2BGR)
    rgb = cv2.inpaint(rgb, m, 9, cv2.INPAINT_TELEA)
    out = np.asarray(im).copy()
    out[..., :3] = cv2.cvtColor(rgb, cv2.COLOR_BGR2RGB)
    out[..., 3] = 255
    return Image.fromarray(out)


def unswizzle(level0, w, h):
    """a 32 bit console texture's texels in rows (they are kept in Morton order, x's bit first)"""
    idx = np.zeros((h, w), np.int64)
    for axis, n in ((1, w), (0, h)):
        coord = np.arange(n)
        grid = np.broadcast_to(coord[None, :] if axis else coord[:, None], (h, w))
        bit, shift, ww, hh = 0, 0, w, h
        out = np.zeros((h, w), np.int64)
        while ww > 1 or hh > 1:
            if ww > 1:
                if axis == 1: out |= ((grid >> bit) & 1) << shift
                shift += 1
            if hh > 1:
                if axis == 0: out |= ((grid >> bit) & 1) << shift
                shift += 1
            ww, hh, bit = ww >> 1, hh >> 1, bit + 1
        idx |= out
    return np.frombuffer(level0, np.uint8).reshape(-1, 4)[idx]


def logo(z, console):
    """Redux's logo without its name, with a dark glow, laid where the console's has it in its texture (four times its
    size): the game draws that texture in other places too, by the same coordinates"""
    im = redux_picture(z, 'ts2newlogo')
    w, h = im.size
    a = np.asarray(im).copy()
    m = name_mask(im, (int(w * 0.58), int(h * 0.58), int(w * 0.79), int(h * 0.78)))
    a[..., 3][m > 0] = 0
    im = Image.fromarray(a)
    im = im.crop(im.getbbox())
    gw, gh, level0 = console
    alpha = unswizzle(level0, gw, gh)[..., 3]
    ys, xs = np.nonzero(alpha > 16)
    k = 4
    box = (xs.min() * k, ys.min() * k, (xs.max() + 1) * k, (ys.max() + 1) * k)
    scale = min((box[2] - box[0]) / im.width, (box[3] - box[1]) / im.height)
    im = im.resize((round(im.width * scale), round(im.height * scale)), Image.LANCZOS)
    at = ((box[0] + box[2] - im.width) // 2, (box[1] + box[3] - im.height) // 2)
    canvas = Image.new('RGBA', (gw * k, gh * k), (0, 0, 0, 0))
    # a dark glow around it, as the headers have
    shape = Image.new('L', canvas.size, 0)
    shape.paste(im.getchannel('A'), at)
    glow = shape.filter(ImageFilter.MaxFilter(17)).filter(ImageFilter.GaussianBlur(28))
    glow = glow.point(lambda v: min(255, int(v * 1.6)))
    shadow = Image.new('RGBA', canvas.size, (6, 10, 30, 0))
    shadow.putalpha(glow)
    canvas.alpha_composite(shadow)
    canvas.alpha_composite(im, at)
    return canvas


REDUX_HUD = ('arc_bar arc_outline crosshair_arrow crosshair_circle crosshair_laser crosshair_old crosshair_olde '
             'crosshair_shaded crosshair_triangle xbbuttons xbbuttons2 xbcontroller xbcontrollerglow xbstdbutton1 '
             'xbstdbutton2 xbstdbutton3 xbstdbutton4 xbstdbutton5').split()


def hud_pictures(z, data_dir):
    """Redux's reticles, arcs and buttons: every distinct console texture of those names in any of the game's paks (a
    pak may hold its own variant) gets the picture; returns [(name, console (w, h, level0), picture)]"""
    import glob
    found, seen = [], set()
    for path in glob.glob(os.path.join(data_dir, '**', '*.pak'), recursive=True):
        d = open(path, 'rb').read()
        if d[:4] != b'P8CK':
            continue
        directory, count, names = struct.unpack_from('<3I', d, 4)
        for i in range(count):
            name_at, size, offset = struct.unpack_from('<3I', d, directory + i * 12)
            name = d[names + name_at:d.index(b'\0', names + name_at)].decode()
            base = name.split('/')[-1][:-4]
            if not name.endswith('.xbt') or base not in REDUX_HUD or not name.startswith('textures/misc/'):
                continue
            w, h, _, _, fmt = struct.unpack_from('<5I', d, offset)
            level0 = w * h // 2 if fmt == 0 else w * h * 4 if fmt == 1 else w * h
            data = d[offset + 128:offset + 128 + level0]
            key = (base, gpu_hash(data))
            if key in seen:
                continue
            seen.add(key)
            found.append((base, (w, h, data), redux_picture(z, base)))
    return found


def main():
    z = zipfile.ZipFile(redux_path)
    xbox = xbox_textures(xbox_path)
    pictures = [('ts2banner%d' % i, banner(z, 'ts2banner%d' % i)) for i in range(1, 5)]
    pictures += [('ts2smallbanner%d' % i, redux_picture(z, 'ts2smallbanner%d' % i)) for i in range(1, 5)]
    pictures.append(('ts2newlogo', logo(z, xbox['ts2newlogo'])))
    data_dir = os.path.join(os.path.dirname(__file__), '..', 'assets', 'data')
    hud = hud_pictures(z, data_dir)
    out = bytearray(b'HDT1' + struct.pack('<I', len(pictures) + len(hud)))
    view = os.path.join(os.path.dirname(out_path), 'hd_textures')
    os.makedirs(view, exist_ok=True)
    for name, im in pictures:
        gw, gh, level0 = xbox[name]
        px = np.asarray(im.convert('RGBA'))[..., [2, 1, 0, 3]].tobytes()
        out += name.encode().ljust(32, b'\0')
        out += struct.pack('<3IQ2I', gw, gh, len(level0), gpu_hash(level0), im.width, im.height)
        out += px
        im.save(os.path.join(view, name + '.png'))
        print('%-16s %dx%d for %dx%d' % (name, im.width, im.height, gw, gh))
    for name, (gw, gh, level0), im in hud:
        px = np.asarray(im.convert('RGBA'))[..., [2, 1, 0, 3]].tobytes()
        out += name.encode().ljust(32, b'\0')
        out += struct.pack('<3IQ2I', gw, gh, len(level0), gpu_hash(level0), im.width, im.height)
        out += px
        print('%-20s %dx%d for %dx%d' % (name, im.width, im.height, gw, gh))
    open(out_path, 'wb').write(out)
    print(out_path, len(out), 'bytes')


main()
