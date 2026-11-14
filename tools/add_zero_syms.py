import sys
p, miss = sys.argv[1], sys.argv[2]
s = open(p).read()
names = [l.strip() for l in open(miss) if l.strip()]
add = ''.join(f"{n:<52} = 0\n" for n in names)
marker = "\n[data]"
assert marker in s
s = s.replace(marker, "\n# not in TS2: the host traps if the game calls one\n" + add + marker, 1)
open(p, 'w').write(s)
print(len(names))
