#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""RGSS1_iatcheck.py <pristine_dir> <v1_dir>
检验冻结于 OEP 的 dump：
  1) 还原头里 DataDirectory[1]（导入目录）长啥样、能否直接走描述符
  2) 全镜像指针扫描：多少 dword 落在各模块区间（用 v1 同 boot 的 modmaps 映射）
"""
import struct, sys, os

def load(p):
    return open(p, 'rb').read()

def main(pris, v1):
    dump = load(os.path.join(pris, 'dump.bin'))
    pe = struct.unpack_from('<I', dump, 0x3c)[0]
    nsec = struct.unpack_from('<H', dump, pe+6)[0]
    optsz = struct.unpack_from('<H', dump, pe+20)[0]
    opt = pe + 24
    print(f"PE@{pe:#x} nsec={nsec} optsz={optsz:#x}")
    secoff = opt + optsz
    for i in range(nsec):
        nm = dump[secoff+40*i:secoff+40*i+8].rstrip(b'\0').decode('latin1')
        vs, va, rs, ra = struct.unpack_from('<IIII', dump, secoff+40*i+8)
        ch = struct.unpack_from('<I', dump, secoff+40*i+36)[0]
        print(f"  sec[{i}] {nm:8s} VA={va:#010x} VS={vs:#x} RS={rs:#x} chars={ch:#010x}")
    dd = opt + 96
    for idx, nm in [(0,'export'),(1,'import'),(2,'resource'),(5,'reloc'),(12,'IAT')]:
        rva, sz = struct.unpack_from('<II', dump, dd+8*idx)
        print(f"  DD[{idx}] {nm:8s} RVA={rva:#x} SZ={sz:#x}")

    # 尝试直接走导入描述符
    irva, isz = struct.unpack_from('<II', dump, dd+8)
    def rva_ok(r):
        for i in range(nsec):
            vs, va, rs, ra = struct.unpack_from('<IIII', dump, secoff+40*i+8)
            if va <= r < va + max(vs, rs): return True
        return False
    if irva and rva_ok(irva):
        print("--- import descriptor walk ---")
        cnt = 0
        off = irva
        while cnt < 40:
            oft, ts, fc, nameRva, ft = struct.unpack_from('<IIIII', dump, off)
            if not nameRva and not ft: break
            nm = b'?'
            for i in range(nsec):
                vs, va, rs, ra = struct.unpack_from('<IIII', dump, secoff+40*i+8)
                if va <= nameRva < va + max(vs, rs):
                    o = ra + (nameRva - va)
                    nm = dump[o:dump.find(b'\0', o)]
                    break
            # IAT 槽实值（前 3 个）
            vals = []
            for k in range(3):
                v = struct.unpack_from('<I', dump, ft + 4*k)[0]
                vals.append(v)
            print(f"  desc[{cnt}] {nm.decode('latin1','replace'):16s} OFT={oft:#x} FT={ft:#x} firstthunks={[hex(v) for v in vals]}")
            off += 20; cnt += 1
        print(f"  ({cnt} descriptors)")
    else:
        print("--- import dir NOT usable (rva=%#x) ---" % irva)

    # 模块区间（v1 同 boot）
    mods = []
    for ln in open(os.path.join(v1, 'modules.txt')):
        b, s, n = ln.rstrip('\n').split('\t')
        mods.append((int(b, 16), int(s, 16), n))
    apphelp = [m for m in mods if 'apphelp' in m[2].lower()]
    print("--- pointer scan ---")
    stat = {}
    apphelp_hits = []
    for off in range(0x1000, len(dump)-4, 4):
        v = struct.unpack_from('<I', dump, off)[0]
        for base, size, name in mods:
            if base <= v < base + size:
                key = name.split('\\')[-1].lower()
                stat[key] = stat.get(key, 0) + 1
                if 'apphelp' in key:
                    apphelp_hits.append((off, v))
                break
    for k in sorted(stat, key=stat.get, reverse=True)[:15]:
        print(f"  {k:24s} {stat[k]}")
    print(f"  apphelp slots in target image: {len(apphelp_hits)}")
    if 0 < len(apphelp_hits) <= 40:
        for off, v in apphelp_hits[:40]:
            print(f"    RVA {off:#010x} -> {v:#010x}")

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
