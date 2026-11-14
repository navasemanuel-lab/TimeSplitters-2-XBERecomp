"""TimeSplitters 1 (PS2) level -> TimeSplitters 2 Xbox level (.xbr), the format of Chemical Plant in TSPC's xbob.pak.

python tools/ts1_to_xbr.py <level.raw> <out.xbr> [first texture id]      (also writes <out>.textures.txt: new id, TS1 id)

What the format is (read from TSPC's Chemical Plant xbr, which came from the PS2 level; 12815 of the PS2 level's 13072 triangles
are in it, and the game's own loader code, 0x1C2A0 / 0x1D200 / 0x1D250 / 0x129840 / 0x14A3E0):
  header 16 B  {texture table 0x20, rooms (array of 0xB0 B records), portal pointer list, 0}; at 0x10 {0, -1, -1, 0}
  texture table: 16 B {id, 0, 0, flags} to {-1, 0, 0, 0}; a room's chunks name a texture by its place in this table
  rooms: record 0 is empty, then the rooms, then a record of zeros. Room n (numbered from 1) is record n:
      +0 geometry (the model header, 0x34 B, {1 node, ..., 1.0f at +0x28, +0x34 = the texture list's count}), +4 its texture list
      {count, indexes}, +8 the end of that list, +0xC the room's bounding box, +0x24 the floors' box, +0x3C the walls' box (y from
      the bottom to the top), +0x54 a box of the portals (empty when none), +0x6C the box that says which room a point is in
  the room's data, before its model header: {chunk table, GIF headers, colours, positions, texture coordinates} (a chunk is
      {texture, k, first vertex, vertices, 0}; its first vertex a multiple of 4; the arrays are the node's pass 0), the floors
      (80 B {4, 0, 0, 0, xz box, 4 corners}) and the walls (44 B {0, bottom end xyz, top end (x, top, z), xz box}), the pointer
      array of both, the strip tables {offset, count} to u16 triangle index lists, then the node (0x9C B) and the header
      node word 2 = walls << 16 | floors, +0x6C the pointers (floors then walls), +0x70 the walls', +0x14 the chunk table,
      +0x24..: GIF headers, positions, texture coordinates, colours, 0; +0x60 vertices | triangles << 16; +0x90 the strip table
  portals: 72 B each, copied as they are; the pointer list {0, pointers..., 0}
In the PS2 file the texture list, floors, walls and box columns of record k are room k's, but the geometry column is one record
behind: room k's geometry is the one record k-1 points to (room 0 has none)."""
import os, struct, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ts1level as L

FMAX = 3.4028234663852886e+38
NODE = 0x9C
MAX_TRIS = 24    # a chunk's triangles at most, as the game's own models


def U(d, o): return struct.unpack_from('<I', d, o)[0]


def ptr_list(d, a):
    out = []
    while a and U(d, a):
        out.append(U(d, a)); a += 4
        if len(out) > 4096: raise ValueError('runaway list')
    return out


class Out:
    def __init__(s): s.b = bytearray()
    def put(s, data, align=4):
        while len(s.b) % align: s.b.append(0)
        at = len(s.b); s.b += data
        return at


def box_union(a, b):
    if a is None: return b
    if b is None: return a
    return (np.minimum(a[0], b[0]), np.maximum(a[1], b[1]))


def pack_box(b):
    if b is None: return struct.pack('<6f', FMAX, FMAX, FMAX, -FMAX, -FMAX, -FMAX)
    return struct.pack('<6f', *[float(v) for v in b[0]], *[float(v) for v in b[1]])


def chunk_groups(g):
    """[(material, [triangle (a, b, c) of PS2 vertex indices])] with at most MAX_TRIS triangles each"""
    out = []
    for mat, tris in L.triangles(g):
        for i in range(0, len(tris), MAX_TRIS):
            out.append((mat, tris[i:i + MAX_TRIS]))
    return out


def room_arrays(g):
    """the node's pass 0 from a decoded PS2 geometry -> (desc, gif, colours, positions, uvs, strip lists, vertices, triangles)"""
    desc = b''; gif = b''; col = b''; pos = b''; st = b''; lists = []
    first = 0; ntri = 0
    for k, (mat, tris) in enumerate(chunk_groups(g)):
        used = []; seen = {}
        for t in tris:
            for v in t:
                if v not in seen: seen[v] = len(used); used.append(v)
        while first % 4:    # a chunk's first vertex is a multiple of 4
            col += b'\0\0\0\0'; pos += struct.pack('<4f', 0, 0, 0, 1); st += struct.pack('<3f', 0, 0, 1); first += 1
        n = len(used)
        gif += struct.pack('<4I', n | 0x8000, 0x302E4000, 0x412, 0)
        desc += struct.pack('<5H', mat, k, first, n, 0)
        for v in used:
            p = g['pos'][v]; u = g['uv'][v]
            pos += struct.pack('<4f', float(p[0]), float(p[1]), float(p[2]), 1.0)
            st += struct.pack('<3f', float(u[0]), float(u[1]), 1.0)
            col += bytes(g['rgba'][v])
        lists.append([first + seen[v] for t in tris for v in t])
        ntri += len(tris); first += n
    desc += struct.pack('<5H', 0, 0, 0, 0, 0xFFFF)
    return desc, gif, col, pos, st, lists, first, ntri


def floor_polys(d, p2):
    out = []; seen = set()
    for a in ptr_list(d, p2):
        n = U(d, a); w = struct.unpack_from('<12f', d, a + 8)
        key = (n,) + tuple(round(v, 4) for v in w)
        if key in seen: continue
        seen.add(key); out.append((n, np.array(w, np.float64).reshape(4, 3)))
    return out


def wall_polys(d, p3):
    out = []
    for a in ptr_list(d, p3):
        w = struct.unpack_from('<8f', d, a + 4)   # x0 y0 z0 top0  x1 y1 z1 top1
        out.append(w)
    return out


def build(ps2, first_tex):
    tex = L.textures(ps2)
    rooms = L.rooms(ps2)
    R = len(rooms)
    o = Out()
    o.b += struct.pack('<4I', 0x20, 0, 0, 0) + struct.pack('<4I', 0, 0xFFFFFFFF, 0xFFFFFFFF, 0)
    tex_at = o.put(b''.join(struct.pack('<4I', first_tex + i, t[1], t[2], t[3]) for i, t in enumerate(tex)) +
                   struct.pack('<4I', 0xFFFFFFFF, 0, 0, 0))
    assert tex_at == 0x20
    records = [b'\0' * 0xB0]
    stats = dict(rooms=R, empty=0, tris=0, floors=0, walls=0)
    for pi in range(R):
        ptrs, bmin, bmax = rooms[pi]
        g = None
        if pi >= 1:
            gp = rooms[pi - 1][0][0]
            g = L.room_geometry(ps2, gp)
        if g is None: stats['empty'] += 1
        # chunk table, arrays
        desc_at = gif_at = col_at = pos_at = st_at = 0; strips_at = 0; nvert = ntri = 0
        if g is not None:
            desc, gif, col, pos, st, lists, nvert, ntri = room_arrays(g)
            desc_at = o.put(desc); gif_at = o.put(gif); col_at = o.put(col); pos_at = o.put(pos); st_at = o.put(st)
            stats['tris'] += ntri
        # floors and walls
        floors = floor_polys(ps2, ptrs[2]); walls = wall_polys(ps2, ptrs[3])
        stats['floors'] += len(floors); stats['walls'] += len(walls)
        fl_at = []; fbox = None
        for n, c in floors:
            x0, z0, x1, z1 = c[:, 0].min(), c[:, 2].min(), c[:, 0].max(), c[:, 2].max()
            body = struct.pack('<4I', 4, 0, 0, 0)[:4] + struct.pack('<3I', 0, 0, 0) + struct.pack('<4f', x0, z0, x1, z1) + \
                struct.pack('<12f', *c.reshape(-1))
            fl_at.append(o.put(body)); fbox = box_union(fbox, (c.min(0), c.max(0)))
        wl_at = []; wbox = None
        for w in walls:
            x0, y0, z0, t0, x1, y1, z1, t1 = w
            top = max(t0, t1)
            body = struct.pack('<I', 0) + struct.pack('<6f', x0, y0, z0, x1, top, z1) + \
                struct.pack('<4f', min(x0, x1), min(z0, z1), max(x0, x1), max(z0, z1))
            wl_at.append(o.put(body))
            wbox = box_union(wbox, (np.array([min(x0, x1), min(y0, y1), min(z0, z1)]), np.array([max(x0, x1), top, max(z0, z1)])))
        arr_at = o.put(struct.pack('<%dI' % len(fl_at + wl_at), *(fl_at + wl_at))) if fl_at + wl_at else 0
        # strip tables and their index lists
        if g is not None:
            table = o.put(b'\0' * (8 * len(lists))); strips_at = table
            for k, lst in enumerate(lists):
                at = o.put(struct.pack('<%dH' % len(lst), *lst))
                struct.pack_into('<II', o.b, table + 8 * k, at, len(lst))
        node = [0] * (NODE // 4)
        node[0] = 0xFFFF0000; node[1] = 0xFFFF
        node[2] = (len(wl_at) << 16) | len(fl_at)
        node[5] = desc_at
        node[9], node[10], node[11], node[12] = gif_at, pos_at, st_at, col_at
        node[24] = (nvert & 0xFFFF) | (min(ntri, 0xFFFF) << 16)
        node[27] = arr_at; node[28] = arr_at + 4 * len(fl_at) if arr_at else 0
        node[35] = 0x3F800000
        node[36] = strips_at
        tl = U(ps2, ptrs[1]); tex_list = struct.unpack_from('<%dI' % (tl + 1), ps2, ptrs[1])
        hdr = bytearray(0x34); struct.pack_into('<I', hdr, 0, 1); struct.pack_into('<I', hdr, 0x28, 0x3F800000)
        blob = o.put(struct.pack('<%dI' % (NODE // 4), *node) + bytes(hdr) + struct.pack('<%dI' % len(tex_list), *tex_list) + bytes(4))
        hdr_at = blob + NODE; list_at = hdr_at + 0x34; end_at = list_at + 4 * len(tex_list)
        # the record
        rb = struct.pack('<3I', hdr_at, list_at, end_at) + pack_box((np.array(bmin), np.array(bmax)))
        walls_box = wbox
        both = box_union(fbox, wbox)
        rb += pack_box(fbox) + pack_box(walls_box) + pack_box(None) + pack_box(both)
        records.append(rb.ljust(0xB0, b'\0'))
    records.append(b'\0' * 0xB0)
    # portals
    pl = []; a = U(ps2, 8) + 4
    while U(ps2, a): pl.append(U(ps2, a)); a += 4
    new = [o.put(bytes(ps2[p:p + 72])) for p in pl]
    list_at = o.put(struct.pack('<%dI' % (len(new) + 2), 0, *new, 0))
    rooms_at = o.put(b''.join(records), 16)
    struct.pack_into('<4I', o.b, 0, 0x20, rooms_at, list_at, 0)
    stats['portals'] = len(pl); stats['bytes'] = len(o.b)
    return bytes(o.b), [t[0] for t in tex], stats


if __name__ == '__main__':
    ps2 = open(sys.argv[1], 'rb').read()
    first = int(sys.argv[3]) if len(sys.argv) > 3 else 31300
    xbr, ids, stats = build(ps2, first)
    open(sys.argv[2], 'wb').write(xbr)
    open(sys.argv[2] + '.textures.txt', 'w').write('\n'.join('%d %d' % (first + i, t) for i, t in enumerate(ids)))
    print(stats)
