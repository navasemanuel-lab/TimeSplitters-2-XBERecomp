"""python -I tools/set_sym.py config/game.toml Name=0xADDR ...   (replaces an existing [symbols] line)"""
import sys, re
p = sys.argv[1]; s = open(p).read()
for kv in sys.argv[2:]:
    n, a = kv.split('=')
    s, c = re.subn(rf'(?m)^{re.escape(n)}\s+= .*$', f'{n:<52} = {a}', s)
    assert c == 1, n
open(p, 'w').write(s)
