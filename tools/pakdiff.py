"""Compare two P8CK paks by entry name and size: python -I tools/pakdiff.py A.pak B.pak [regex]"""
import re, struct, sys


def p8(path):
    d = open(path, 'rb').read()
    tab, n, names = struct.unpack_from('<3I', d, 4)
    out = {}
    for i in range(n):
        no, sz, off = struct.unpack_from('<3I', d, tab + 12 * i)
        try:
            out[d[names + no:d.index(b'\0', names + no)].decode()] = (sz, d[off:off + sz])
        except ValueError:
            pass
    return out


if __name__ == '__main__':
    A, B = p8(sys.argv[1]), p8(sys.argv[2])
    rx = re.compile(sys.argv[3] if len(sys.argv) > 3 else '.')
    print(len(A), 'entries in A,', len(B), 'in B')
    print('only in A:', sorted(k for k in A if k not in B and rx.search(k)))
    print('only in B:', sorted(k for k in B if k not in A and rx.search(k)))
    same = diff = 0
    changed = []
    for k in sorted(A):
        if k in B and rx.search(k):
            if A[k][1] == B[k][1]:
                same += 1
            else:
                diff += 1
                changed.append((k, A[k][0], B[k][0]))
    print('identical', same, 'different', diff)
    for k, a, b in changed:
        print('  ', k, a, b)
