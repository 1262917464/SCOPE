import struct, sys

RT = {1:'CURSOR',2:'BITMAP',3:'ICON',4:'MENU',5:'DIALOG',6:'STRING',7:'FONTDIR',
      8:'FONT',9:'ACCEL',10:'RCDATA',11:'MSGTABLE',12:'GRPCURSOR',14:'GRPICON',
      16:'VERSION',17:'DLGINCLUDE',19:'PLUGPLAY',20:'VXD',21:'ANICURSOR',
      22:'ANIICON',23:'HTML',24:'MANIFEST'}


def pe(p):
    d = open(p, 'rb').read()
    e = struct.unpack_from('<I', d, 0x3c)[0]
    nsec = struct.unpack_from('<H', d, e+6)[0]
    optsz = struct.unpack_from('<H', d, e+20)[0]
    opt = e + 24
    magic = struct.unpack_from('<H', d, opt)[0]
    dd = opt + (96 if magic == 0x10b else 112)
    rrva = struct.unpack_from('<I', d, dd+16)[0]
    so = opt + optsz
    secs = []
    for i in range(nsec):
        nm, vs, va, rs, ro = struct.unpack_from('<8sIIII', d, so+i*40)
        secs.append((nm.rstrip(b'\0').decode('latin1'), va, vs, ro, rs))

    def r2o(r):
        for nm, va, vs, ro, rs in secs:
            if va <= r < va + max(vs, rs) and rs:
                return ro + (r - va)
        return None

    def o2sec(o):
        for nm, va, vs, ro, rs in secs:
            if rs and ro <= o < ro + rs:
                return nm
        return '?'

    return d, secs, r2o, o2sec, rrva


def walk(p, verbose=False):
    d, secs, r2o, o2sec, rrva = pe(p)
    base = r2o(rrva)
    if base is None:
        print("  no .rsrc")
        return []
    rows = []

    def rd_name(off):
        ln = struct.unpack_from('<H', d, off)[0]
        return d[off+2:off+2+ln*2].decode('utf-16le')

    def go(off, path):
        n_named, n_id = struct.unpack_from('<HH', d, off+12)
        n = n_named + n_id
        for i in range(n):
            name, sub = struct.unpack_from('<II', d, off+16+i*8)
            if name & 0x80000000:
                lab = rd_name(base + (name & 0x7fffffff))
                is_str = True
            else:
                lab = name
                is_str = False
            if sub & 0x80000000:
                go(base + (sub & 0x7fffffff), path+[(lab, is_str)])
            else:
                doff = base + sub
                drva, dsz, cp, _ = struct.unpack_from('<IIII', d, doff)
                fo = r2o(drva)
                rows.append((path + [(lab, is_str)], dsz, drva, fo, cp))
    go(base, [])
    return rows, d


for p in sys.argv[1:]:
    print("=" * 74)
    print(p)
    rows, d = walk(p)
    from collections import Counter
    types = Counter()
    for path, sz, rva, fo, cp in rows:
        t = path[0][0]
        types[RT.get(t, t) if not path[0][1] else t] += 1
    print("  types:", dict(types))
    for path, sz, rva, fo, cp in rows:
        t = path[0][0]
        tn = RT.get(t, str(t)) if not path[0][1] else str(t)
        if tn in ('MENU', 'DIALOG', 'RCDATA', 'MANIFEST', 'VERSION', 'STRING', 'ICON', 'GRPICON', 'BITMAP'):
            nm = path[1][0]
            nm = RT.get(nm, nm) if len(path) > 1 and not path[1][1] else nm
            print("    %-9s name=%-26s size=%7d rva=%#08x off=%s" %
                  (tn, nm, sz, rva, hex(fo) if fo else None))
    # name strings inside RCDATA (DFM / Delphi form)
    for path, sz, rva, fo, cp in rows:
        t = path[0][0]
        tn = RT.get(t, t) if not path[0][1] else t
        if tn == 'RCDATA' and fo:
            blob = d[fo:fo+sz]
            print("    RCDATA[%s] head=%r" % (path[1][0], blob[:80]))
