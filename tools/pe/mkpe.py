#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
mkpe.py — 把 unpacker.exe 的内存 dump 组装成可分析 PE（重建 IAT）
用法: python mkpe.py <dumpdir> <out.dll>

输入: dumpdir/{dump.bin,modules.txt,modmaps.txt}
输出: out.dll + dumpdir/imports_report.txt
文件布局约定: dump.bin 的文件偏移 == RVA
"""
import struct, sys, os, collections

def align(x, a): return (x + a - 1) & ~(a - 1)

def main(dumpdir, outpath):
    dump = bytearray(open(os.path.join(dumpdir, 'dump.bin'), 'rb').read())
    mods = []          # [(base,size,name)]
    for ln in open(os.path.join(dumpdir, 'modules.txt')):
        b, s, n = ln.rstrip('\n').split('\t')
        mods.append((int(b, 16), int(s, 16), n))
    # 导出地址映射 addr -> (module_name, func_name)
    expmap = {}
    modof = {}
    for base, size, name in mods:
        modof[base] = (size, name)
    for ln in open(os.path.join(dumpdir, 'modmaps.txt')):
        parts = ln.rstrip('\n').split('\t')
        if len(parts) < 3: continue
        mname, rva, fname = parts[0], int(parts[1], 16), parts[2]
        for base, size, name in mods:
            if mname == name:
                expmap[base + rva] = (name, fname)
                break

    # ---- 解析 dump 的 PE 头 ----
    pe = struct.unpack_from('<I', dump, 0x3c)[0]
    nsec = struct.unpack_from('<H', dump, pe + 6)[0]
    optsz = struct.unpack_from('<H', dump, pe + 20)[0]
    opt = pe + 24
    magic = struct.unpack_from('<H', dump, opt)[0]
    assert magic == 0x10b, 'not PE32'
    ddoff = opt + 96
    imgsize = struct.unpack_from('<I', dump, opt + 56)[0]
    print('dump: 0x%x bytes, SizeOfImage=0x%x, nsec=%d' % (len(dump), imgsize, nsec))

    # ---- 扫描 IAT: 同模块连续导出地址指针段 ----
    runs = []   # (start_rva, count, module_name)
    i = 0
    n = len(dump)
    def owner(a):
        for base, size, name in mods:
            if base <= a < base + size:
                return name
        return None
    cur_start = None; cur_mod = None; cur_cnt = 0
    for off in range(0x1000, n - 4, 4):
        v = struct.unpack_from('<I', dump, off)[0]
        m = expmap.get(v)
        if m:
            modname = m[0]
            if cur_mod == modname and cur_cnt:
                cur_cnt += 1
            else:
                if cur_cnt >= 2:
                    runs.append((cur_start, cur_cnt, cur_mod))
                cur_start = off; cur_mod = modname; cur_cnt = 1
        else:
            # 允许 IAT 中间夹少量非指针(如已改成别的数据), 但断开更稳: 直接断
            if cur_cnt >= 2:
                runs.append((cur_start, cur_cnt, cur_mod))
            cur_start = None; cur_mod = None; cur_cnt = 0
    if cur_cnt >= 2:
        runs.append((cur_start, cur_cnt, cur_mod))

    # 合并小 run (相邻同模块, 间隔<=8 dword)
    merged = []
    for r in runs:
        if merged:
            p = merged[-1]
            if r[2] == p[2] and r[0] - (p[0] + 4 * p[1]) <= 32:
                p[1] = (r[0] + 4 * r[1] - p[0]) // 4
                continue
        merged.append([r[0], r[1], r[2]])
    runs = merged
    print('IAT runs found: %d' % len(runs))

    # 按模块聚合报告
    rep = collections.defaultdict(list)
    for start, cnt, mod in runs:
        for k in range(cnt):
            a = struct.unpack_from('<I', dump, start + 4 * k)[0]
            nm = expmap.get(a, ('?', 'ord_%X' % a))[1]
            rep[mod].append((start + 4 * k, nm))
    with open(os.path.join(dumpdir, 'imports_report.txt'), 'w') as f:
        for mod in sorted(rep):
            f.write('=== %s (%d thunks) ===\n' % (mod, len(rep[mod])))
            names = sorted(set(nm for _, nm in rep[mod]))
            f.write('  ' + ', '.join(names) + '\n')
    for mod in sorted(rep):
        print('  %-12s %d imports, e.g. %s' % (os.path.basename(mod), len(rep[mod]),
              ', '.join(sorted(set(n for _, n in rep[mod]))[:6])))

    # ---- 合成新导入节 ----
    # 只给系统 DLL 建 descriptor (排除自身/target)
    keep = [r for r in runs if 'RGSS20' not in r[2] and 'unpacker' not in r[2]]
    newsec_va = align(imgsize, 0x1000)
    buf = bytearray()
    desc_off = len(buf)
    ndesc = len(keep)
    buf += b'\x00' * (20 * (ndesc + 1))
    name_ptrs = []
    for start, cnt, mod in keep:
        name_ptrs.append(len(buf))
        buf += os.path.basename(mod).lower().encode('ascii', 'replace') + b'\x00'
    while len(buf) % 4: buf += b'\x00'
    # 写 descriptor: OFT=0, Time=0, Fwd=0, Name=rva, FirstThunk=start_rva
    for i, (start, cnt, mod) in enumerate(keep):
        struct.pack_into('<IIIII', buf, desc_off + 20 * i, 0, 0, 0,
                         newsec_va + name_ptrs[i], start)
    newsec_size = align(len(buf), 0x1000)

    out = bytearray(dump)
    out += buf
    out += b'\x00' * (newsec_va + newsec_size - len(out))
    # 更新节表: 追加 .imp2
    secoff = opt + optsz
    struct.pack_into('<8sIIIIIIHHI', out, secoff + 40 * nsec,
                     b'.imp2\x00\x00\x00\x00', newsec_size, newsec_va, newsec_size,
                     newsec_va, 0, 0, 0, 0, 0x40000040)
    struct.pack_into('<H', out, pe + 6, nsec + 1)
    struct.pack_into('<I', out, opt + 56, newsec_va + newsec_size)  # SizeOfImage
    struct.pack_into('<I', out, opt + 4 + 16, 0x1000)               # SectionAlignment 不变; FileAlignment=0x1000
    # 旧节 raw 指到 VA (offset==RVA 布局)
    for i in range(nsec):
        so = secoff + 40 * i
        vs, va = struct.unpack_from('<II', out, so + 8)
        struct.pack_into('<II', out, so + 16, align(max(vs, 4), 0x1000), va)  # SizeOfRawData, PtrRaw
    # DataDirectory[1] -> 新 descriptor; [12] IAT 清零; [0] 导出目录保持内存中已恢复的
    struct.pack_into('<II', out, ddoff + 8, newsec_va + desc_off, 20 * (ndesc + 1))
    struct.pack_into('<II', out, ddoff + 12 * 8, 0, 0)
    open(outpath, 'wb').write(out)
    print('written %s (0x%x bytes)' % (outpath, len(out)))

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
