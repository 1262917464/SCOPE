import struct, sys

def info(p):
    d = open(p, 'rb').read()
    print("=" * 74)
    print(p, len(d))
    e = struct.unpack_from('<I', d, 0x3c)[0]
    if d[e:e+4] != b'PE\0\0':
        print("NOT PE"); return
    mach, nsec, ts, sym, nsym, optsz, chars = struct.unpack_from('<HHIIIHH', d, e+4)
    print("machine=%#x sections=%d timestamp=%d optsz=%d chars=%#x" % (mach, nsec, ts, optsz, chars))
    opt = e + 24
    magic = struct.unpack_from('<H', d, opt)[0]
    ep = struct.unpack_from('<I', d, opt+16)[0]
    ib = struct.unpack_from('<I', d, opt+28)[0]
    sa = struct.unpack_from('<I', d, opt+32)[0]
    fa = struct.unpack_from('<I', d, opt+36)[0]
    subsys = struct.unpack_from('<H', d, opt+68)[0]
    print("optmagic=%#x entry=%#x imagebase=%#x sectalign=%#x filealign=%#x subsys=%d dllchars=%#x" %
          (magic, ep, ib, sa, fa, subsys, struct.unpack_from('<H', d, opt+70)[0]))
    secoff = opt + optsz
    for i in range(nsec):
        n, vs, va, rs, ro = struct.unpack_from('<8sIIII', d, secoff + i*40)
        ch = struct.unpack_from('<I', d, secoff + i*40 + 36)[0]
        print("  %-10s va=%#010x vs=%#010x raw=%#010x rs=%#010x ch=%#010x" %
              (n.rstrip(b'\0').decode('latin1'), va, vs, ro, rs, ch))
    # imports
    dd = opt + (96 if magic == 0x10b else 112)
    imp_rva, imp_sz = struct.unpack_from('<II', d, dd + 8)
    print("import dir rva=%#x size=%#x" % (imp_rva, imp_sz))
    return d

def rva2off(d, e, optsz, rva):
    nsec = struct.unpack_from('<H', d, e+6)[0]
    secoff = e + 24 + optsz
    for i in range(nsec):
        n, vs, va, rs, ro = struct.unpack_from('<8sIIII', d, secoff + i*40)
        if va <= rva < va + max(vs, rs):
            return ro + (rva - va)
    return None

for p in sys.argv[1:]:
    d = info(p)
    e = struct.unpack_from('<I', d, 0x3c)[0]
    optsz = struct.unpack_from('<H', d, e+20)[0]
    opt = e + 24
    magic = struct.unpack_from('<H', d, opt)[0]
    dd = opt + (96 if magic == 0x10b else 112)
    imp_rva = struct.unpack_from('<I', d, dd + 8)[0]
    off = rva2off(d, e, optsz, imp_rva)
    if off is None:
        print("  no import table"); continue
    i = 0
    while True:
        ent = d[off + i*20: off + i*20 + 20]
        if len(ent) < 20 or ent == b'\0'*20: break
        oft, tds, fc, name_rva, ft = struct.unpack('<IIIII', ent)
        no = rva2off(d, e, optsz, name_rva)
        nm = d[no:d.index(b'\0', no)].decode('latin1') if no else '?'
        # count thunks
        to = rva2off(d, e, optsz, oft or ft)
        cnt = 0
        if to:
            while True:
                v = struct.unpack_from('<I', d, to + cnt*4)[0]
                if v == 0: break
                cnt += 1
        print("  IMP %-24s thunks=%d" % (nm, cnt))
        i += 1
