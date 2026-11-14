"""Exception code, address and registers of a minidump: python -I tools/dmp_info.py <dump>"""
import struct, sys

d = open(sys.argv[1], 'rb').read()
n, rva = struct.unpack_from('<II', d, 8)[0], struct.unpack_from('<I', d, 12)[0]
streams = {}
for i in range(n):
    t, sz, off = struct.unpack_from('<III', d, rva + i * 12)
    streams[t] = (sz, off)
if 6 in streams:
    sz, off = streams[6]
    tid, _ = struct.unpack_from('<II', d, off)
    code, flags, rec, addr, npar = struct.unpack_from('<IIQQI', d, off + 8)
    print('thread', tid, 'code %08X' % code, 'address %016X' % addr)
    params = struct.unpack_from('<15Q', d, off + 8 + 4 + 4 + 8 + 8 + 4 + 4)
    print('params', [hex(p) for p in params[:npar]])
    ctxsz, ctxrva = struct.unpack_from('<II', d, off + 8 + 152)
    names = ['P1Home', 'P2Home', 'P3Home', 'P4Home', 'P5Home', 'P6Home', 'ContextFlags', 'MxCsr']
    # x64 CONTEXT: segs/flags then Rax.. at 0x78
    regs = struct.unpack_from('<16Q', d, ctxrva + 0x78)
    rn = 'rax rcx rdx rbx rsp rbp rsi rdi r8 r9 r10 r11 r12 r13 r14 r15'.split()
    rip, = struct.unpack_from('<Q', d, ctxrva + 0xF8)
    print('rip %016X' % rip)
    print(' '.join('%s=%X' % (a, b) for a, b in zip(rn, regs)))
if 4 in streams:
    sz, off = streams[4]
    cnt, = struct.unpack_from('<I', d, off)
    for i in range(cnt):
        base, size, _, _, nrva = struct.unpack_from('<QIIII', d, off + 4 + i * 108)
        ln, = struct.unpack_from('<I', d, nrva)
        name = d[nrva + 4:nrva + 4 + ln].decode('utf-16le')
        if 'TS2' in name or 'ntdll' in name.lower():
            print('%016X %X %s' % (base, size, name))
