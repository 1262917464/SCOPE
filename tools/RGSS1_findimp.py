#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""RGSS1_findimp.py <pristine_dir> <v1_dir>
在冻结 dump 的解压数据中寻找原始 IMAGE_IMPORT_DESCRIPTOR 链。
验证：每个描述符的 FT 槽（运行期已解析的指针）须命中 modmaps 中同名模块的导出。
输出：描述符链 RVA、每描述符模块名/槽数/命中数，以及 IAT 区间。
"""
import struct, sys, os

def build_expmap(v1dir):
    mods = []
    for ln in open(os.path.join(v1dir, 'modules.txt')):
        b, s, n = ln.rstrip('\n').split('\t')
        mods.append((int(b, 16), int(s, 16), n))
    expmap = {}
    for ln in open(os.path.join(v1dir, 'modmaps.txt')):
        parts = ln.rstrip('\n').split('\t')
        if len(parts) < 3: continue
        mname, rva, fname = parts[0], int(parts[1], 16), parts[2]
        if mname == 'TARGET': continue
        for base, size, name in mods:
            if mname == name:
                expmap.setdefault(base + rva, (name.split('\\')[-1].lower(), fname))
                break
    return mods, expmap

def main(dumpdir, v1dir):
    d = open(os.path.join(dumpdir, 'dump.bin'), 'rb').read()
    imgsize = len(d)
    mods, expmap = build_expmap(v1dir)

    def modof(va):
        for base, size, name in mods:
            if base <= va < base + size:
                return name.split('\\')[-1].lower()
        return None

    def rva_str(rva, maxlen=40):
        if not (0 < rva < imgsize): return None
        e = d.find(b'\0', rva)
        if e < 0 or e - rva > maxlen: return None
        s = d[rva:e]
        if not all(32 <= c < 127 for c in s): return None
        return s.decode('latin1')

    def check_desc(D):
        """返回 (name, nslots, hits, ft) 或 None"""
        oft, ts, fc, nameRva, ft = struct.unpack_from('<IIIII', d, D)
        if not nameRva or not ft or nameRva >= imgsize or ft >= imgsize: return None
        nm = rva_str(nameRva)
        if not nm or not nm.lower().endswith('.dll'): return None
        if any(c in nm for c in ' /\\'): return None
        th = oft if oft else ft
        if th >= imgsize: return None
        want = nm.lower()
        n, hits = 0, 0
        while n < 4096:
            v = struct.unpack_from('<I', d, ft + 4*n)[0]
            if v == 0: break
            m = expmap.get(v)
            if m and (m[0] == want or m[0].startswith(want[:-4])):
                hits += 1
            n += 1
        if n == 0: return None
        return (nm, n, hits, ft)

    best = None
    for D in range(0x1000, min(imgsize - 20, 0x196000), 4):
        if d[D:D+20] == b'\0'*20: continue
        first = check_desc(D)
        if not first or first[2] < max(2, first[1]//2): continue
        # 链式：后续描述符也须基本有效
        chain, off, bad = [], D, 0
        while off + 20 <= imgsize and len(chain) < 32:
            c = check_desc(off)
            if c is None:
                if d[off:off+20] == b'\0'*20:
                    chain.append(None); off += 20; break
                bad += 1; break
            chain.append(c); off += 20
        if not chain or chain[-1] is None or bad or len(chain) < 3: continue
        tot = sum(c[1] for c in chain if c)
        score = sum(c[2] for c in chain if c)
        if best is None or score > best[0]:
            best = (score, D, chain, tot)
    if not best:
        print("NO descriptor chain found")
        return
    score, D, chain, tot = best
    print(f"found chain @RVA {D:#x} (score {score}/{tot} slots matched)")
    lo = min(c[3] for c in chain if c)
    hi = max(c[3] + 4*c[1] for c in chain if c)
    for i, c in enumerate(chain):
        if c is None: print(f"  desc[{i}] <end>"); continue
        print(f"  desc[{i}] {c[0]:16s} slots={c[1]:3d} hits={c[2]:3d} FT={c[3]:#x}")
    print(f"IAT region: {lo:#x} .. {hi:#x} ({(hi-lo)//4} dwords)")
    print(f"import dir RVA={D:#x} size={20*len(chain):#x}")

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
