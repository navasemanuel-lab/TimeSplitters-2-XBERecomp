"""Names the sounds of a game.log's 'audio: play' lines by their size in sounds.pak: python -I tools/audio_names.py <log> [from s]"""
import re, sys
sys.path.insert(0, __file__.rsplit('tools', 1)[0] + 'tools')
from pak import entries
d, names, es = entries('../data/sounds.pak')
by = {}
for a, b, c in es:
    by.setdefault(b - 32, []).append(names[a:names.index(b'\0', a)].decode()[4:-4])
t0 = float(sys.argv[2]) if len(sys.argv) > 2 else 0
for l in open(sys.argv[1], encoding='utf-8', errors='replace'):
    m = re.match(r'\[\s*([\d.]+)\]\s*audio: (play|pause|stop|buffer \w+ same) (\w+)?(.*)', l)
    if not m or float(m.group(1)) < t0: continue
    mm = re.search(r'bytes (\d+)', l)
    print(m.group(1), m.group(2), m.group(3), m.group(4)[:30], ','.join(by.get(int(mm.group(1)), ['?'])[:3]) if mm else '')
