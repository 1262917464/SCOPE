import struct, sys, re

RT = {1:'CURSOR',2:'BITMAP',3:'ICON',4:'MENU',5:'DIALOG',6:'STRING',7:'FONTDIR',
      8:'FONT',9:'ACCEL',10:'RCDATA',11:'MSGTABLE',12:'GRPCURSOR',14:'GRPICON',
      16:'VERSION',17:'DLGINCLUDE',24:'MANIFEST'}


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
    return d, secs, r2o, rrva


def resources(p):
    d, secs, r2o, rrva = pe(p)
    base = r2o(rrva)
    out = []
    def rd_name(off):
        ln = struct.unpack_from('<H', d, off)[0]
        return d[off+2:off+2+ln*2].decode('utf-16le')
    def go(off, path):
        n_named, n_id = struct.unpack_from('<HH', d, off+12)
        for i in range(n_named + n_id):
            name, sub = struct.unpack_from('<II', d, off+16+i*8)
            lab = rd_name(base + (name & 0x7fffffff)) if (name & 0x80000000) else name
            if sub & 0x80000000:
                go(base + (sub & 0x7fffffff), path+[lab])
            else:
                doff = base + sub
                drva, dsz, cp, _ = struct.unpack_from('<IIII', d, doff)
                out.append((path+[lab], dsz, drva, r2o(drva)))
    go(base, [])
    return out, d


def dump_dfm(blob):
    """Decode a binary DFM (TPF0) stream into an indented tree."""
    if blob[:4] != b'TPF0':
        return None
    pos = [4]
    out = []
    def rb():
        v = blob[pos[0]]; pos[0] += 1; return v
    def rw():
        v = struct.unpack_from('<H', blob, pos[0])[0]; pos[0] += 2; return v
    def rdw():
        v = struct.unpack_from('<I', blob, pos[0])[0]; pos[0] += 4; return v
    def rstr():
        n = rb()
        if n == 0:
            n = rw()
            if n == 0:
                n = rdw()
        s = blob[pos[0]:pos[0]+n].decode('latin1'); pos[0] += n
        return s
    def rvalue(depth=0):
        t = rb()
        if t == 0x00: return None                       # vaNull
        if t == 0x01:                                   # vaList
            vals = []
            while True:
                save = pos[0]
                if blob[pos[0]] == 0:
                    pos[0] += 1; break
                vals.append(rvalue(depth+1))
            return vals
        if t == 0x02:
            v = struct.unpack_from('<b', blob, pos[0])[0]; pos[0] += 1; return v
        if t == 0x03:
            v = struct.unpack_from('<h', blob, pos[0])[0]; pos[0] += 2; return v
        if t == 0x04:
            v = rdw(); return v - (1 << 32) if v >= (1 << 31) else v
        if t == 0x05:
            pos[0] += 10; return '<extended>'
        if t == 0x06: return rstr()
        if t == 0x07: return '#' + rstr()
        if t == 0x08: return False
        if t == 0x09: return True
        if t == 0x0A:
            n = rdw(); b = blob[pos[0]:pos[0]+n]; pos[0] += n
            return '<binary %d>' % n
        if t == 0x0B:                                   # vaSet
            items = []
            while True:
                s = rstr()
                if not s: break
                items.append(s)
            return items
        if t == 0x0C:                                   # vaCollection
            items = []
            while True:
                if blob[pos[0]] == 0:
                    pos[0] += 1; break
                # collection item: optional index, then properties
                idx = None
                if blob[pos[0]] == 0x02 or blob[pos[0]] == 0x03 or blob[pos[0]] == 0x04:
                    idx = rvalue()
                props = []
                while True:
                    if blob[pos[0]] == 0:
                        pos[0] += 1; break
                    pn = rstr()
                    props.append((pn, rvalue(depth+1)))
                items.append((idx, props))
            return items
        if t == 0x0D:
            pos[0] += 4; return '<single>'
        if t == 0x0E:
            pos[0] += 8; return '<currency>'
        if t == 0x0F:
            pos[0] += 8; return '<date>'
        if t == 0x10: return rstr()
        if t == 0x11:
            v = struct.unpack_from('<q', blob, pos[0])[0]; pos[0] += 8; return v
        if t == 0x12: return rstr()
        if t == 0x13:
            pos[0] += 8; return '<double>'
        return '<unk %#x>' % t
    def robj(depth):
        cls = rstr()
        name = rstr()
        props = []
        while True:
            if blob[pos[0]] == 0:
                pos[0] += 1; break
            pn = rstr()
            props.append((pn, rvalue()))
        out.append(('  '*depth) + '%s : %s' % (name or '<root>', cls))
        for k, v in props:
            sv = repr(v)
            if len(sv) > 110: sv = sv[:110] + '...'
            out.append(('  '*depth) + '    .%s = %s' % (k, sv))
        while pos[0] < len(blob) and blob[pos[0]] == 0x01:
            pos[0] += 1
            robj(depth+1)
    try:
        robj(0)
    except Exception as ex:
        out.append('  <decode stopped: %r at %d>' % (ex, pos[0]))
    return out


for p in sys.argv[1:]:
    print("=" * 76)
    print(p)
    rows, d = resources(p)
    for path, sz, rva, fo in rows:
        tn = path[0]
        nm = path[1] if len(path) > 1 else ''
        tname = RT.get(tn, str(tn))
        if tname == 'RCDATA' and nm == 'PACKAGEINFO' and fo:
            blob = d[fo:fo+sz]
            strs = [s.decode('latin1') for s in re.findall(rb'[A-Za-z_][A-Za-z0-9_\.@]{1,60}', blob)]
            seen = []
            for s in strs:
                if s not in seen: seen.append(s)
            print("  PACKAGEINFO (%d bytes) unit/name stream:" % sz)
            print("   ", ', '.join(seen))
        elif tname == 'RCDATA' and nm == 'DVCLAL' and fo:
            print("  DVCLAL =", d[fo:fo+sz].hex())
        elif tname == 'RCDATA' and str(nm).upper().startswith('TFORM') and fo:
            print("  DFM %s (%d bytes):" % (nm, sz))
            tree = dump_dfm(d[fo:fo+sz])
            for line in (tree or ['<not TPF0>']):
                print("   ", line)
