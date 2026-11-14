"""Builds data/button_icons.bin, the pictures of the pad's buttons and of the keyboard's keys and the mouse's buttons that
the prompts of the menus and the pause screen show (src/common/button_icons.cpp): python -I tools/make_button_icons.py
[source folder] [out.bin]

The source folder holds Keyboard_Mouse, XGamepad, P4Gamepad, P5Gamepad and SGamepad, each with an "Alt" folder of 128x128
pictures. Each picture is kept at 96x96 (resized with its colour weighted by its alpha), as BGRA.

"BIC1", icon count, texture count; then each icon: name (32 bytes), width, height, its BGRA pixels; then each texture of
the console's that the prompts are drawn from (xbstdbutton1-4, stdbutton5 and xbbuttons, found in the game's paks, one
entry for each variant of them there): name (32 bytes), console width, height, bytes hashed, hash (u64), as gpu::Hash
hashes the level 0 of a console texture (the renderer looks a replacement picture up by them)."""
import glob
import os
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = sys.argv[1] if len(sys.argv) > 1 else r'C:\Users\navas\Pictures\Source'
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, '..', 'data', 'button_icons.bin')
SIZE = 96

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


def load(folder, name):
    return Image.open(os.path.join(SRC, folder, 'Alt', name)).convert('RGBA')


# the keyboard's and the mouse's: the pack's name -> its file in Keyboard_Mouse/Alt
KEYS = {}
for c in 'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789':
    KEYS['k_' + c] = 'T_%s_Key_Alt.png' % c
KEYS['k_4'] = 'T_3_Key_Alt-1.png'  # the pack has no T_4: its second 3 is the 4
for n, f in {
    'Space': 'Space', 'Enter': 'Enter', 'Esc': 'Esc', 'Tab': 'Tab', 'Backspace': 'BackSpace', 'Shift': 'Shift',
    'Ctrl': 'Crtl', 'Alt': 'Alt', 'CapsLock': 'CapsLock', 'Up': 'Up', 'Down': 'Down', 'Left': 'Left', 'Right': 'Right',
    'Home': 'Home', 'End': 'End', 'PageUp': 'PageUp', 'PageDown': 'PageDown', 'Ins': 'Ins', 'Del': 'Del',
    'PrtScrn': 'PrtScrn', 'NumLock': 'NumLock', 'Minus': 'Minus', 'Plus': 'Plus', 'Asterisk': 'Asterisk',
    'Slash': 'Slash', 'Semicolon': 'Semicolon', 'Quote': 'Quotation', 'Question': 'Question_Mark',
    'BracketL': 'Brackets_L', 'BracketR': 'Brackets_R', 'Tilde': 'Tilde', 'Period': 'Keyboard_R',
    'MouseLeft': 'Mouse_Left', 'MouseRight': 'Mouse_Right', 'MouseMiddle': 'Mouse_Middle',
    'MouseOther': 'Mouse_Simple', 'Cursor': 'Cursor',
}.items():
    KEYS['k_' + n] = 'T_%s_Key_Alt.png' % f
KEYS['k_Comma'] = 'T_Keyboard_R_Key_Alt-1.png'
KEYS['k_WheelUp'] = 'T_Mouse_Scroll_Up_Key_Dark_Key_Alt.png'
KEYS['k_WheelDown'] = 'T_Mouse_Scroll_Down_Key_Dark_Key_Alt.png'
for i in range(1, 13):
    KEYS['k_F%d' % i] = 'T_F%d_Key_Alt.png' % i


# the pads': the physical places of the buttons (south east west north) as each family names them
PADS = {
    'x_a': ('XGamepad', 'T_X_A_Color_Alt.png'), 'x_b': ('XGamepad', 'T_X_B_Color_Alt.png'),
    'x_x': ('XGamepad', 'T_X_X_Color_Alt.png'), 'x_y': ('XGamepad', 'T_X_Y_Color_Alt.png'),
    'x_back': ('XGamepad', 'T_X_Share_Alt.png'),
    'p4_cross': ('P4Gamepad', 'T_P4_Cross_Color_Alt.png'), 'p4_circle': ('P4Gamepad', 'T_P4_Circle_Color_Alt.png'),
    'p4_square': ('P4Gamepad', 'T_P4_Square_Color_Alt.png'), 'p4_triangle': ('P4Gamepad', 'T_P4_Triangle_Color_Alt.png'),
    'p4_options': ('P4Gamepad', 'T_P4_Options_Alt.png'), 'p4_share': ('P4Gamepad', 'T_P4_Share_Alt.png'),
    'p5_cross': ('P5Gamepad', 'T_P5_Cross_Color_Alt.png'), 'p5_circle': ('P5Gamepad', 'T_P5_Circle_Color_Alt.png'),
    'p5_square': ('P5Gamepad', 'T_P5_Square_Color_Alt.png'), 'p5_triangle': ('P5Gamepad', 'T_P5_Triangle_Color_Alt.png'),
    'p5_options': ('P5Gamepad', 'T_P5_Options_Alt.png'), 'p5_share': ('P5Gamepad', 'T_P5_Share_Alt.png'),
    's_a': ('SGamepad', 'T_S_A_Alt.png'), 's_b': ('SGamepad', 'T_S_B_Alt.png'), 's_x': ('SGamepad', 'T_S_X_Alt.png'),
    's_y': ('SGamepad', 'T_S_Y_Alt.png'), 's_plus': ('SGamepad', 'T_S_Plus_Alt.png'),
    's_minus': ('SGamepad', 'T_S_Minus_Alt.png'),
}


def xbox_start(ref):
    """The pack has no Menu button: a disc and ring like its white A's, with the three lines"""
    k = 4
    s = 128 * k
    im = Image.new('RGBA', (s, s), (0, 0, 0, 0))
    a = np.asarray(ref)
    # the disc's colour at its middle-left and the ring's at its edge, taken from the white A
    disc = tuple(int(v) for v in a[64, 30, :3])  # the white A's dark disc, between its ring and its letter
    ring = (255, 255, 255)
    d = ImageDraw.Draw(im)
    d.ellipse((4 * k, 4 * k, s - 4 * k, s - 4 * k), fill=ring + (255,))
    d.ellipse((10 * k, 10 * k, s - 10 * k, s - 10 * k), fill=disc + (255,))
    for y in (46, 64, 82):
        d.rounded_rectangle((38 * k, (y - 5) * k, 90 * k, (y + 5) * k), radius=5 * k, fill=ring + (255,))
    return im.resize((128, 128), Image.LANCZOS)


def small(im):
    return im.resize((SIZE, SIZE), Image.LANCZOS)


def bgra(im):
    return np.asarray(im.convert('RGBA'))[..., [2, 1, 0, 3]].tobytes()


def console_textures():
    """every textures/misc/<name>.xbt of the buttons in the game's paks: (name, w, h, bytes hashed, hash), once for each
    distinct picture"""
    want = {'xbstdbutton1', 'xbstdbutton2', 'xbstdbutton3', 'xbstdbutton4', 'stdbutton5', 'xbbuttons'}
    found, seen = [], set()
    data_dir = os.path.join(HERE, '..', 'assets', 'data')
    for path in glob.glob(os.path.join(data_dir, '**', '*.pak'), recursive=True):
        d = open(path, 'rb').read()
        if d[:4] != b'P8CK':
            continue
        directory, count, names = struct.unpack_from('<3I', d, 4)
        for i in range(count):
            name_at, size, offset = struct.unpack_from('<3I', d, directory + i * 12)
            name = d[names + name_at:d.index(b'\0', names + name_at)].decode()
            base = name.split('/')[-1][:-4]
            if not name.endswith('.xbt') or base not in want or not name.startswith('textures/misc/'):
                continue
            w, h, _, _, fmt = struct.unpack_from('<5I', d, offset)
            level0 = w * h // 2 if fmt == 0 else w * h * 4 if fmt == 1 else w * h
            h64 = gpu_hash(d[offset + 128:offset + 128 + level0])
            if (base, h64) in seen:
                continue
            seen.add((base, h64))
            found.append((base, w, h, level0, h64))
    return found


def main():
    icons = {}
    for name, f in KEYS.items():
        icons[name] = small(load('Keyboard_Mouse', f))
    for name, (folder, f) in PADS.items():
        icons[name] = small(load(folder, f))
    icons['x_start'] = small(xbox_start(load('XGamepad', 'T_X_A_White_Alt.png')))
    textures = console_textures()
    print(len(icons), 'icons,', len(textures), 'console textures')
    for t in textures:
        print('  ', t)
    out = bytearray(b'BIC1' + struct.pack('<2I', len(icons), len(textures)))
    for name, im in icons.items():
        out += name.encode().ljust(32, b'\0') + struct.pack('<2I', im.width, im.height) + bgra(im)
    for base, w, h, n, hv in textures:
        out += base.encode().ljust(32, b'\0') + struct.pack('<3IQ', w, h, n, hv)
    os.makedirs(os.path.dirname(os.path.abspath(OUT)), exist_ok=True)
    open(OUT, 'wb').write(out)
    print(OUT, len(out), 'bytes')


main()
