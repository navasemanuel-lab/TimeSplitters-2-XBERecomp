"""Phobos split2-recomp config/us/xdk_symbols.json -> a recompiler config xbe_symbols --sign can read.
Only names the SDK already knows (xdk/5849/signatures.toml) are kept, exact match."""
import json, re, sys
src, sdk_sigs, out = sys.argv[1:4]
j = json.load(open(src))
sig = open(sdk_sigs).read()
known = set(re.findall(r'^(\w+) = ', sig, re.M))
syms, data = {}, {}
for x in j['symbols']:
    n = x['name']
    if n not in known or x['address'] < 0x1000:
        continue
    (syms if x['kind'] == 'function' else data)[n] = x['address']
with open(out, 'w') as f:
    f.write('[xbe]\npath = "assets/default.xbe"\ncode_sections = [".text", "D3D", "XGRPH", "DSOUND", "XNET", "XPP"]\n\n[symbols]\n')
    for n, a in sorted(syms.items()): f.write(f'{n} = 0x{a:X}\n')
    f.write('\n[data]\n')
    for n, a in sorted(data.items()): f.write(f'{n} = 0x{a:X}\n')
print(len(syms), 'functions', len(data), 'data')
