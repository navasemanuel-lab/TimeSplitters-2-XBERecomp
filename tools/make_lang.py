"""Writes lang/<language>.txt (F1 panel translations) from tools/lang_tr.txt and the source texts in src/common/lang.cpp:
python -I make_lang.py <out lang dir>   (dump_lang.py's JSON is rebuilt here by calling its logic)"""
import json, os, subprocess, sys, tempfile
here = os.path.dirname(os.path.abspath(__file__))
tmp = os.path.join(tempfile.gettempdir(), 'ts2_pairs.json')
subprocess.check_call([sys.executable, '-I', os.path.join(here, 'dump_lang.py'), tmp])
pairs = json.load(open(tmp, encoding='utf-8'))
def unesc(s):
    return s.replace(chr(92) + '"', '"').replace(chr(92) * 2, chr(92))
es_of = {unesc(en).rstrip(): unesc(es) for es, en in pairs}
raw = open(os.path.join(here, 'lang_tr.txt'), encoding='utf-8').read().replace('\r', '')
blocks = [b for b in raw.split('\n\n')]
blocks[0] = '\n'.join(l for l in blocks[0].split('\n') if not l.startswith('# '))
NAMES = ['francais', 'deutsch', 'italiano', 'portugues']
out = {n: [] for n in NAMES}
seen = set()
for b in blocks:
    b = b.strip('\n')
    if not b:
        continue
    ls = b.split('\n')
    assert len(ls) == 5, (len(ls), ls[0][:50])
    en = ls[0].rstrip()
    assert en in es_of, en
    seen.add(en)
    for n, t in zip(NAMES, ls[1:]):
        es = es_of[en]
        if es.endswith(' ') and not t.endswith(' '): t += ' '
        out[n].append(es + ' = ' + t)
missing = [e for e in es_of if e not in seen]
assert not missing, missing
os.makedirs(sys.argv[1], exist_ok=True)
for n in NAMES:
    open(os.path.join(sys.argv[1], n + '.txt'), 'w', encoding='utf-8', newline='\r\n').write(
        '# F1 panel translation (UTF-8). Left of " = " is the source text and must stay as written.\n' + '\n'.join(out[n]) + '\n')
    print(n, len(out[n]))
