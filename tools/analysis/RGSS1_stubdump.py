"""RGSS1 line: print ImageBase and extract packer stub bytes around EP."""
import struct, sys, os

def secmap(d, e):
    nsec = struct.unpack_from('<H', d, e+6)[0]
    opt = e + 24
    optsz = struct.unpack_from('<H', d, e+20)[0]
    sec_off = opt + optsz
    secs = []
    for i in range(nsec):
        raw = d[sec_off+i*40:sec_off+i*40+8]
        name = raw.rstrip(b'\0').decode('ascii', 'replace').strip()
        vs, va, rs, ra = struct.unpack_from('<IIII', d, sec_off+i*40+8)
        secs.append((name, va, vs, ra, rs))
    return secs, opt

def doit(path, outdir):
    d = open(path, 'rb').read()
    e = struct.unpack_from('<I', d, 0x3C)[0]
    secs, opt = secmap(d, e)
    imagebase = struct.unpack_from('<I', d, opt+28)[0]
    ep = struct.unpack_from('<I', d, opt+16)[0]
    print(f"== {path}")
    print(f"   ImageBase={imagebase:#010x} EP_RVA={ep:#x}")
    off = None
    for name, va, vs, ra, rs in secs:
        if va <= ep < va + max(vs, rs):
            off = ra + (ep - va)
            secname = name
            print(f"   EP in section '{name}' -> file offset {off:#x}")
            break
    if off is None:
        print("   !! EP not mapped"); return
    blob = d[off:off+0x2000]
    base = os.path.basename(path).replace('.dll', '')
    out = os.path.join(outdir, f"{base}_stub.bin")
    open(out, 'wb').write(blob)
    print(f"   stub bytes -> {out} ({len(blob)} bytes)")

outdir = r'E:\rgss2-rebuild\unpacked\RGSS1_stubs'
os.makedirs(outdir, exist_ok=True)
for p in [r'E:\rgss2-rebuild\backup\RGSS102J.dll',
          r'E:\rgss2-rebuild\backup\RGSS103J.dll',
          r'E:\rgss2-rebuild\backup\RGSS104E.dll']:
    doit(p, outdir)
