import struct, sys

path = 'test/Project3_orig/RGSS202E.dll'
d = open(path, 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]
nsec = struct.unpack_from('<H', d, pe + 6)[0]
optsz = struct.unpack_from('<H', d, pe + 20)[0]
imgbase = struct.unpack_from('<I', d, pe + 24 + 28)[0]
secs = []
so = pe + 24 + optsz
for i in range(nsec):
    o = so + i * 40
    vs, va, rs, ro = struct.unpack_from('<IIII', d, o + 8)
    secs.append((va, vs, ro, rs))

def rva2off(rva):
    for va, vs, ro, rs in secs:
        if va <= rva < va + max(vs, rs):
            return ro + (rva - va)
    return None

def cstr(off):
    e = d.index(b'\0', off)
    return d[off:e].decode('latin1')

imp_rva, imp_sz = struct.unpack_from('<II', d, pe + 24 + 96 + 8)
off = rva2off(imp_rva)
iat = {}
i = 0
while True:
    o = off + i * 20
    oft, ts, fc, name_rva, ft = struct.unpack_from('<IIIII', d, o)
    if name_rva == 0:
        break
    dll = cstr(rva2off(name_rva))
    thunk_rva = oft if oft else ft
    to = rva2off(thunk_rva)
    slot = ft
    j = 0
    while True:
        v = struct.unpack_from('<I', d, to + j * 4)[0]
        if v == 0:
            break
        if v & 0x80000000:
            nm = '#%d' % (v & 0xFFFF)
        else:
            nm = cstr(rva2off(v) + 2)
        iat[imgbase + slot + j * 4] = '%s!%s' % (dll, nm)
        j += 1
    i += 1

for addr in sorted(iat):
    if addr >= 0x100FF3C0:
        print('0x%08X = %s' % (addr, iat[addr]))
