"""TimeSplitters 1 (PS2, SLUS-20090) level geometry: bg/levelN/levelN.raw of BG.PAK.

python tools/ts1level.py <level.raw> [out.obj]

Header: +0 the texture table's offset (16 B records {id, 0, 0, n} to 0xFFFFFFFF), +4 the level struct's, +8 the table of the
portals' offsets (72 B each). The level struct (0x4DC in Chemical Plant) holds the rooms, 44 B each:
  {geometry, texture list, floor polygons, wall polygons, ?, bbox min xyz, bbox max xyz}
A room's geometry: a 0x30 B preamble ({1, 0...}), the strips' GIF headers (16 B: {n | 0x8000 or 0x10000 as a group's end,
0x302E4000, 0x412, 0}), the positions (xyz floats, 12 B), the texture coordinates ({u, v, 1, 1}, 16 B), the colours (RGBA bytes),
and the chunk table: 6 u32 {material, first strip, strips, first vertex, vertices, kind} to a record of 0xFFFFFFFF. A chunk's
vertices are padded to a multiple of 4; each strip is a triangle strip of its own."""
import struct, sys
import numpy as np


def U(d, o): return struct.unpack_from('<I', d, o)[0]


def textures(d):
    o = U(d, 0); out = []
    while U(d, o) != 0xFFFFFFFF:
        out.append(struct.unpack_from('<4I', d, o)); o += 16
    return out


def rooms(d):
    """[(geometry, texlist, floors, walls, p4, bbox min, bbox max)]; the table starts 44 bytes into the level struct"""
    o = U(d, 4) + 44; out = []
    while 0 < U(d, o) < len(d):
        w = struct.unpack_from('<5I6f', d, o)
        out.append((w[:5], w[5:8], w[8:11])); o += 44
    return out


def room_geometry(d, g):
    """{'strips': [n], 'pos', 'uv', 'rgba', 'chunks': [(material, first strip, strips, first vertex, vertices)]}"""
    o = g + 0x30; hdr = []; flip = []
    while U(d, o + 4) == 0x302E4000:
        hdr.append(U(d, o) & 0x7FFF); flip.append(bool(U(d, o) & 0x10000)); o += 16
    # the chunk table is after the arrays: find it from its end (0xFFFFFFFF in the kind of a record) by walking the
    # arrays' size: vertices of the chunks, padded to 4
    n = len(hdr); V = o
    # try every plausible vertex count: the chunk table starts right after 32 bytes a vertex and must read as records
    # whose strips run 0..n and whose vertices follow each other
    for N in range(4, 4 + 4 * 40000, 4):
        t = V + 32 * N
        if t + 24 > len(d): break
        recs = []; p = t; first = 0; strip = 0; ok = True
        while True:
            w = struct.unpack_from('<6I', d, p)
            if w[2] == 0 and strip == n: break  # the terminator: a record of zeros
            if w[1] != strip or w[3] != first or w[5] != 0 or w[2] == 0 or len(recs) > 400:
                ok = False; break
            recs.append(w[:5]); strip += w[2]; first += (w[4] + 3) & ~3; p += 24
        if ok and recs and strip == n and first == N:
            pos = np.frombuffer(d[V:V + 12 * N], '<f4').reshape(-1, 3)
            uv = np.frombuffer(d[V + 12 * N:V + 28 * N], '<f4').reshape(-1, 4)
            rgba = np.frombuffer(d[V + 28 * N:V + 32 * N], np.uint8).reshape(-1, 4)
            return dict(strips=hdr, flip=flip, pos=pos, uv=uv, rgba=rgba, chunks=recs, end=p + 24)
    return None


def triangles(g):
    """[(material, [(a, b, c)])] vertex indices into g['pos']; a strip's triangles alternate their winding, all turned when the header has 0x10000 (measured against Chemical Plant's Xbox rooms: 12815 of the 13072 triangles found, none turned the wrong way)"""
    out = []; strips = g['strips']
    for mat, s0, ns, v0, nv in g['chunks']:
        tris = []; v = v0
        for k in range(s0, s0 + ns):
            n = strips[k]
            for i in range(2, n):
                a, b, c = v + i - 2, v + i - 1, v + i
                t = (a, b, c) if i % 2 == 0 else (b, a, c)
                tris.append((t[0], t[2], t[1]) if g['flip'][k] else t)  # bit 0x10000 of the strip's header: its faces the other way
            v += n
        out.append((mat, tris))
    return out


def main():
    d = open(sys.argv[1], 'rb').read()
    rs = rooms(d); tex = textures(d)
    print('textures', len(tex), 'rooms', len(rs))
    obj = []; base = 1; bad = 0; nt = 0
    for i, (ptrs, lo, hi) in enumerate(rs):
        g = room_geometry(d, ptrs[0])
        if not g:
            bad += 1; print('room', i, 'geometry not decoded'); continue
        obj.append('o room%d' % i)
        for p in g['pos']: obj.append('v %f %f %f' % (p[0], p[1], p[2]))
        for mat, tris in triangles(g):
            for a, b, c in tris:
                obj.append('f %d %d %d' % (base + a, base + b, base + c)); nt += 1
        base += len(g['pos'])
    print('rooms decoded', len(rs) - bad, 'bad', bad, 'triangles', nt)
    if len(sys.argv) > 2: open(sys.argv[2], 'w').write('\n'.join(obj))


if __name__ == '__main__':
    main()
