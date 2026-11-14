"""Byte-scan code sections for absolute stores (mov [addr],reg / mov [addr],imm / mov [addr],eax): python -I tools/store_scan.py <hex>"""
import sys, struct, re, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from xbe_re import Xbe
x = Xbe('assets/default.xbe'); t = int(sys.argv[1], 16); pat = re.escape(struct.pack('<I', t))
for s in x.code_sections():
    data = x.raw[s[3]:s[3] + s[4]]
    for m in re.finditer(pat, data):
        i = m.start()
        if data[i-1:i] == b'\xa3' or (data[i-2:i-1] in (b'\x89', b'\x88', b'\xc7', b'\xc6', b'\xd9', b'\xdd', b'\x66') and data[i-1] & 0xC7 == 5):
            print('%08X  %s  [%s]' % (s[1] + i - (1 if data[i-1:i] == b'\xa3' else 2), data[i-2:i+8].hex(), s[0]))
