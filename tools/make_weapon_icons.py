"""Builds data/weapon_icons.bin, the weapon bar's atlas, from the icon sheet (a grid of outlined guns) and a capture of the
fist: python -I tools/make_weapon_icons.py <gun.pak> [out.bin]

Each gun's outline in the sheet is turned into a smooth silhouette (the shape grown to many times its size, softened and
cut again, so its edge is clean at any size) and drawn as the bar draws them: a silhouette with a clean stroke and, for
the one in hand, a dark one with a white stroke. Silenced guns and the Temporal Uplink are drawn here.

The atlas is 2048x1280 RGBA of 256x128 cells: rows 0-4 the icons as the bar shows them, rows 5-9 as the chosen one.
Cell n is column n % 8, row n // 8 (+5 for the chosen)."""
import os
import struct
import sys

import numpy as np
from PIL import Image, ImageChops, ImageDraw, ImageFilter

sheet_path = sys.argv[1]
out_path = sys.argv[2] if len(sys.argv) > 2 else 'data/weapon_icons.bin'

# atlas order: the game's weapon table (entries 0 to 34, then the Uplink and the Crossbow)
ORDER = ['fist', 'Silenced Pistol', 'Silenced Pistol x2', 'Silenced Luger', 'Silenced Luger x2', 'Luger Pistol',
         'Luger Pistol x2', 'Garrett', 'Garrett x2', 'Tactical 12', 'Minigun', 'Sniper Rifle', 'Vintage Rifle',
         'Flamethrower', 'Fire Extinguisher', 'S47', 'S47 x2', 'ElectroTool', 'Scifi Handgun', 'Rocket Launcher',
         'Homing Launcher', 'Lasergun', 'Plasma Autorifle', 'Grenade Launcher', 'Proximity Mine', 'Remote Mine',
         'Timed Mine', 'TNT', 'Tommy', 'Tommy x2', 'SBP90', 'SBP90 x2', 'Shotgun', 'Shotgun x2', 'Brick', 'Uplink',
         'Crossbow']
W, H = 256, 128
SS = 2  # the shape is made at this many times the cell, then brought down

# the guns' promo models (gun.pak ob/guns/<name>_promo.xbr: the whole gun, in side view, as the gun sheet shows them);
# the brick has none: its mask from tools/_sil
MODEL = {'Silenced Pistol': 'silencedpistol', 'Luger Pistol': 'luger', 'Silenced Luger': 'silencedluger',
         'Garrett': 'magnum', 'Tactical 12': 'assaultshotgun', 'Minigun': 'minigun', 'Sniper Rifle': 'sniperrifle',
         'Vintage Rifle': 'sniperbolt', 'Flamethrower': 'flamethrower', 'Fire Extinguisher': 'fireext',
         'S47': 'ak47', 'ElectroTool': 'electrogun', 'Scifi Handgun': 'scifihandgun', 'Rocket Launcher': 'rocketlauncher',
         'Homing Launcher': 'homingrocketlauncher', 'Lasergun': 'laser', 'Plasma Autorifle': 'plasmamachinegun',
         'Grenade Launcher': 'grenadelauncher', 'Proximity Mine': 'proxmine', 'Remote Mine': 'remotemine',
         'Timed Mine': 'timedmine', 'TNT': 'tnt', 'Tommy': 'tommygun', 'SBP90': 'uzi',
         'Shotgun': 'doublebarrelshotgun', 'Brick': None, 'Uplink': 'mapboy', 'Crossbow': 'crossbow'}
AXES = {'Crossbow': (2, 0)}  # seen from above: its prod spreads sideways
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import promo_sil
PAK = promo_sil.pak(sheet_path)


def model_shape(name):
    """the gun's side silhouette, from its promo model, at SS times the cell; 'x2' = two of them"""
    double = name.endswith(' x2')
    base = name[:-3] if double else name
    key = MODEL[base]
    if key is None:
        m = Image.open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '_sil', 'brick_ph.png')).convert('L')
    else:
        m = promo_sil.mask(promo_sil.triangles(PAK['ob/guns/%s_promo.xbr' % key], (0, 1, 2)), W=2400, pad=2,
                           axes=AXES.get(base, (2, 1)))
    m = m.crop(m.getbbox())
    full = Image.new('L', (W * SS, H * SS), 0)
    if not double:
        box = (W - 24) * SS, (H - 24) * SS
        k = min(box[0] / m.width, box[1] / m.height)
        g = m.resize((max(1, round(m.width * k)), max(1, round(m.height * k))), Image.LANCZOS).point(lambda v: 255 if v > 128 else 0)
        full.paste(g, ((W * SS - g.width) // 2, (H * SS - g.height) // 2))
    else:
        box = (W - 24 - 40) * SS, (H - 24 - 30) * SS
        k = min(box[0] / m.width, box[1] / m.height)
        g = m.resize((max(1, round(m.width * k)), max(1, round(m.height * k))), Image.LANCZOS).point(lambda v: 255 if v > 128 else 0)
        cx, cy = (W * SS - g.width) // 2, (H * SS - g.height) // 2
        for dx, dy in ((20 * SS, -15 * SS), (-20 * SS, 15 * SS)):  # the second one behind
            full.paste(g, (cx + dx, cy + dy), g)
    return full.transpose(Image.FLIP_LEFT_RIGHT)  # barrels to the left, as the other ports draw them


def smooth(sil, w, h):
    """a smooth shape of the soft silhouette fitted to w x h, at SS times the cell's scale"""
    ys, xs = np.where(sil > 0.35)
    sil = sil[ys.min():ys.max() + 1, xs.min():xs.max() + 1]
    scale = min(w / sil.shape[1], h / sil.shape[0])
    nw, nh = max(1, round(sil.shape[1] * scale * SS)), max(1, round(sil.shape[0] * scale * SS))
    img = Image.fromarray((np.clip(sil, 0, 1) * 255).astype(np.uint8)).resize((nw, nh), Image.BICUBIC)
    step = scale * SS  # one pixel of the source, in pixels of the shape
    img = img.filter(ImageFilter.GaussianBlur(0.7 * step)).point(lambda v: 255 if v > 118 else 0)
    img = img.filter(ImageFilter.GaussianBlur(0.5 * step)).point(lambda v: 255 if v > 128 else 0)  # clean corners
    full = Image.new('L', (W * SS, H * SS), 0)
    full.paste(img, ((W * SS - nw) // 2, (H * SS - nh) // 2))
    return full


def silenced(name):
    """the gun smaller and a suppressor on its barrel: a tube at the muzzle, the muzzle the barrel's far end"""
    sil = sheet_silhouette(NAMES.index(name))
    ys, xs = np.where(sil > 0.35)
    top = ys.min()
    band = sil[top:top + max(4, (ys.max() - top) // 5)] > 0.35
    bx = np.where(band)[1]
    mid = (xs.min() + xs.max()) / 2
    right = abs(bx.max() - mid) > abs(bx.min() - mid)
    tube = 52
    base = smooth(sil, W - 24 - tube, H - 24)
    shift = -tube // 2 if right else tube // 2
    base = ImageChops.offset(base, shift * SS, 0)
    arr = np.asarray(base) > 0
    ys2, xs2 = np.where(arr)
    y_top = ys2.min() // SS
    xm = (xs2.max() if right else xs2.min()) // SS
    x1, x2 = (xm - 4, xm - 4 + tube) if right else (xm + 4 - tube, xm + 4)
    d = ImageDraw.Draw(base)
    d.rounded_rectangle([x1 * SS, (y_top + 1) * SS, x2 * SS, (y_top + 19) * SS], radius=6 * SS, fill=255)
    return base


def uplink():
    """the Temporal Uplink: a hand-held map device, an aerial on the top, a screen in its face, a grip below"""
    img = Image.new('L', (W * SS, H * SS), 0)
    d = ImageDraw.Draw(img)
    s = SS
    d.rounded_rectangle([46 * s, 36 * s, 206 * s, 104 * s], radius=14 * s, fill=255)  # body
    d.rounded_rectangle([86 * s, 98 * s, 128 * s, 124 * s], radius=7 * s, fill=255)  # grip
    d.rounded_rectangle([170 * s, 10 * s, 180 * s, 42 * s], radius=4 * s, fill=255)  # aerial
    d.ellipse([164 * s, 4 * s, 186 * s, 26 * s], fill=255)
    d.rounded_rectangle([64 * s, 48 * s, 160 * s, 88 * s], radius=6 * s, fill=0)  # screen
    d.ellipse([174 * s, 52 * s, 196 * s, 74 * s], fill=0)  # a dial
    d.rounded_rectangle([172 * s, 82 * s, 196 * s, 92 * s], radius=4 * s, fill=0)  # a button
    return img


def extinguisher():
    """the fire extinguisher: a cylinder, its valve and lever on top, the nozzle's horn to one side"""
    img = Image.new('L', (W * SS, H * SS), 0)
    d = ImageDraw.Draw(img)
    s = SS
    d.rounded_rectangle([96 * s, 46 * s, 148 * s, 122 * s], radius=18 * s, fill=255)  # the cylinder
    d.rounded_rectangle([110 * s, 30 * s, 134 * s, 52 * s], radius=6 * s, fill=255)  # the neck
    d.rounded_rectangle([86 * s, 18 * s, 150 * s, 32 * s], radius=6 * s, fill=255)  # the lever
    d.rounded_rectangle([140 * s, 30 * s, 168 * s, 42 * s], radius=5 * s, fill=255)  # the hose's start
    d.polygon([(160 * s, 26 * s), (200 * s, 8 * s), (206 * s, 60 * s), (170 * s, 54 * s)], fill=255)  # the horn
    d.polygon([(166 * s, 36 * s), (194 * s, 24 * s), (198 * s, 52 * s), (172 * s, 48 * s)], fill=0)  # its mouth
    d.rounded_rectangle([108 * s, 64 * s, 136 * s, 90 * s], radius=4 * s, fill=0)  # the label
    return img


def fist():
    """the fist and forearm: the mask saved from the first atlas (tools/fist_mask.png)"""
    f = np.asarray(Image.open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'fist_mask.png')).convert('L')) > 0
    return smooth(f.astype(np.float32), W - 70, H - 40)


def compose(shape_img, chosen):
    """the shape (a mask at SS times) drawn in the bar's style: a dark fill, a white outline round it and a soft white
    glow; the one in hand brighter"""
    m = shape_img
    k = 3 * SS + 1
    outer = m.filter(ImageFilter.MaxFilter(k))
    stroke = ImageChops.subtract(outer, m.filter(ImageFilter.MinFilter(k)))  # centred on the edge, inner details outlined too
    full = outer
    glow = full.filter(ImageFilter.GaussianBlur(4 * SS))

    def down(img):
        return np.asarray(img.resize((W, H), Image.LANCZOS), dtype=np.float32) / 255.0

    O = down(stroke)
    M = down(m)
    halo = down(stroke.filter(ImageFilter.GaussianBlur(3.5 * SS)))  # a very faint light round the edge
    rgba = np.zeros((H, W, 4), np.float32)

    def over(rgb, a):
        a = np.clip(a, 0, 1)[..., None]
        out_a = a + rgba[..., 3:4] * (1 - a)
        c = (np.array(rgb, np.float32) / 255.0 * a + rgba[..., :3] * rgba[..., 3:4] * (1 - a)) / np.maximum(out_a, 1e-4)
        rgba[..., :3] = c
        rgba[..., 3:4] = out_a

    warm = (255, 244, 226)
    over(warm, np.clip(halo * 1.6, 0, 1) * (0.55 if chosen else 0.32))  # the faint glow outside
    over((6, 7, 9), M * 0.97)  # the black silhouette
    over(warm, O * (1.0 if chosen else 0.9))  # the thin warm outline
    return (np.clip(rgba, 0, 1) * 255 + 0.5).astype(np.uint8)


atlas = np.zeros((10 * H, 8 * W, 4), np.uint8)
for n, name in enumerate(ORDER):
    shape = fist() if name == 'fist' else model_shape(name)
    col, row = n % 8, n // 8
    for chosen in (False, True):
        atlas[(row + 5 * chosen) * H:(row + 5 * chosen + 1) * H, col * W:(col + 1) * W] = compose(shape, chosen)

with open(out_path, 'wb') as f:
    f.write(struct.pack('<4sII', b'WIC1', 8 * W, 10 * H))
    f.write(atlas.tobytes())
Image.fromarray(atlas).save(out_path.replace('.bin', '_preview.png'))
print('written', out_path, len(ORDER), 'icons')
