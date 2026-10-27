import struct, sys

def peinfo(path):
    d = open(path, 'rb').read()
    e = struct.unpack_from('<I', d, 0x3C)[0]
    if d[e:e+4] != b'PE\0\0':
        print(f"== {path}: not PE"); return
    mach, nsec = struct.unpack_from('<HH', d, e+4)
    opt = e + 24
    magic = struct.unpack_from('<H', d, opt)[0]
    ep = struct.unpack_from('<I', d, opt+16)[0]
    print(f"== {path}")
    print(f"  machine={mach:#x} nsec={nsec} optmagic={magic:#x} EP_RVA={ep:#x} filesize={len(d)}")
    sec_off = opt + struct.unpack_from('<H', d, e+20)[0]
    secs = []
    for i in range(nsec):
        raw = d[sec_off+i*40:sec_off+i*40+8]
        name = raw.rstrip(b'\0').decode('ascii', 'replace')
        vs, va, rs, ra = struct.unpack_from('<IIII', d, sec_off+i*40+8)
        secs.append((name, va, vs, ra, rs))
        print(f"    {name:10s} VA={va:#010x} VS={vs:#9x} RA={ra:#010x} RS={rs:#9x}")
    def rva2off(r):
        for name, va, vs, ra, rs in secs:
            if va <= r < va + max(vs, rs):
                return ra + (r - va)
        return None
    ddir = opt + (96 if magic == 0x10b else 112)
    exp_rva, exp_sz = struct.unpack_from('<II', d, ddir)
    print(f"  exportdir RVA={exp_rva:#x} size={exp_sz:#x}")
    if exp_rva == 0:
        return
    eo = rva2off(exp_rva)
    if eo is None:
        print("  (export dir not mapped)"); return
    nfunc, nnames = struct.unpack_from('<II', d, eo+20)
    print(f"  exports: {nfunc} funcs, {nnames} names")
    ano_rva = struct.unpack_from('<I', d, eo+32)[0]
    ao = rva2off(ano_rva)
    if ao is None:
        print("  (name table not mapped)"); return
    names = []
    for i in range(min(nnames, 100)):
        p = struct.unpack_from('<I', d, ao+i*4)[0]
        o = rva2off(p)
        if o is None:
            names.append('?'); continue
        s = d[o:d.find(b'\0', o)]
        names.append(s.decode('ascii', 'replace'))
    print("  names:", ' '.join(names))

for p in sys.argv[1:]:
    peinfo(p)
