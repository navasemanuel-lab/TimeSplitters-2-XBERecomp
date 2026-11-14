"""Dumps the panel's source texts (Spanish, English) from src/common/lang.cpp to a JSON list: python -I dump_lang.py <out.json>"""
import re, json, sys
s = open(r'C:\TS2-XBE\recomp\src\common\lang.cpp', encoding='utf-8').read()
a = s.index('kEnglish[] = {')
b = s.index('\n};', a)
q = chr(34)
bs = chr(92)
lit = q + '((?:[^' + q + bs + bs + ']|' + bs + bs + '.)*)' + q
pat = re.compile(r'\{\s*' + lit + r'\s*,\s*' + lit + r'\s*\}', re.S)
ps = pat.findall(s[a:b])
print(len(ps))
json.dump(ps, open(sys.argv[1], 'w', encoding='utf-8'), ensure_ascii=False, indent=0)
