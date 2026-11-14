"""TS2 P8CK pak reader: python -I tools/pak.py <pak> [regex]  lists entries (name, offset, size)."""
import re, struct, sys


def entries(path):
    d = open(path, 'rb').read()
    mg, do, n, no, ns = struct.unpack_from('<4sIIII', d, 0)
    assert mg == b'P8CK', mg
    names = d[no:no + ns]
    out = []
    for i in range(n):
        a, b, c = struct.unpack_from('<III', d, do + i * 12)
        out.append((a, b, c))
    return d, names, out


if __name__ == '__main__':
    d, names, es = entries(sys.argv[1])
    rx = re.compile(sys.argv[2] if len(sys.argv) > 2 else '.')
    print(len(es), 'entries')
    for a, b, c in es[:8]:
        print(hex(a), hex(b), hex(c))
