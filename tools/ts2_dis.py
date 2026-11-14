"""Disassemble TS2 functions by Phobos name (.local/phobos_us_symbols.json), call targets named.
python -I tools/ts2_dis.py <name|hex> [count]      python -I tools/ts2_dis.py --list [regex]"""
import json, re, sys
sys.path.insert(0, __file__.rsplit('tools', 1)[0] + 'tools')
from xbe_re import Xbe
x = Xbe('assets/default.xbe')
syms = json.load(open('.local/phobos_us_symbols.json'))['symbols']
by_name = {s['name']: s['address'] for s in syms}
by_addr = {}
for s in syms:
    by_addr.setdefault(s['address'], s['name'])
if sys.argv[1] == '--list':
    rx = re.compile(sys.argv[2] if len(sys.argv) > 2 else '.')
    for n, a in sorted(by_name.items(), key=lambda kv: kv[1]):
        if rx.search(n): print(f'{a:08X} {n}')
    sys.exit()
a = by_name.get(sys.argv[1]) or int(sys.argv[1], 16)
n = int(sys.argv[2]) if len(sys.argv) > 2 else 60
print(f'; {by_addr.get(a, "")} {a:08X}')
for line in x.dis(a, n):
    m = re.search(r'(call|jmp)\s+0x([0-9a-f]+)', line)
    if m and int(m.group(2), 16) in by_addr: line += f'   ; {by_addr[int(m.group(2), 16)]}'
    print(line)
