"""Find instructions that store to an absolute address: python -I tools/writes_to.py <hex addr>"""
import sys, struct, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from xbe_re import Xbe
x = Xbe('assets/default.xbe'); t = int(sys.argv[1], 16)
for s in x.code_sections():
    for i, ins in enumerate(x.md.disasm(x.raw[s[3]:s[3] + s[4]], s[1])):
        if ins.mnemonic in ('mov', 'add', 'or', 'and', 'inc', 'dec', 'sub', 'xor', 'fstp', 'fst', 'lea') or ins.mnemonic.startswith('set'):
            if '[0x%x]' % t in ins.op_str and (ins.op_str.startswith('dword ptr [0x%x]' % t) or ins.op_str.startswith('byte ptr [0x%x]' % t) or ins.mnemonic in ('fstp', 'fst')):
                print('%08X  %s %s' % (ins.address, ins.mnemonic, ins.op_str))
