#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""RGSS1_mkpe.py — RGSS103J 可运行脱壳 PE 重建
用法: py RGSS1_mkpe.py <dumpdir> <out.dll> <oep_rva> [--probe]

与 VX mkpe2.py 的差异：
  - 导入槽来源 = iatmap.txt（iatmap 由 RGSS1_iatscan.py 产出）
  - 4 个 apphelp 污染槽 → RGSS1_probe.dll 探针（M1 阶段；M2 定名后改 FINAL）
  - 保留壳的 3 节布局（CODE/DATA/.rsrc），只修 RS 与属性
"""
import struct, sys, os

def align(x, a): return (x + a - 1) & ~(a - 1)

PROBE_OVR = {
    0x0367E0: ('RGSS1_probe.dll', 'fix_unknown'),   # 未知语义（无直接引用）
    0x0E730C: ('RGSS1_probe.dll', 'fix_tick3'),     # 3 参、GetTickCount64 系 → 真 GetTickCount
    0x0E73A4: ('RGSS1_probe.dll', 'fix_visible'),   # 1 参 hwnd、可见性类 → 返回 1
    0x0E73C8: ('RGSS1_probe.dll', 'fix_tick5'),     # 5 参、GetTickCount64 系 → 真 GetTickCount
}
SKIP_RUN_CONTAINING = [0x19C0B0]   # 壳自己的 loader 导入表（KERNEL32.DLL/LoadLibraryA…）

def load_slots(dumpdir):
    """从 iatmap.txt 读系统模块槽（剔除 自身/宿主/apphelp/壳loader表）"""
    slots = []
    run_start = None
    for ln in open(os.path.join(dumpdir, 'iatmap.txt')):
        ln = ln.rstrip('\n')
        if ln.startswith('RUN '):
            run_start = int(ln.split('rva=')[1].split('.')[0], 16)
            continue
        if not ln.startswith('  0x') or '!' not in ln:
            continue
        parts = ln.split()
        rva = int(parts[0], 16)
        mod, fn = parts[2].split('!', 1)
        if any(s <= rva < s + 0x40 for s in SKIP_RUN_CONTAINING):
            continue
        if mod.startswith('rgss1') or mod.startswith('rgss10') or mod in ('unpacker.exe','unpacker3.exe'):
            continue
        if mod == 'apphelp.dll':
            continue
        if fn == '?':
            continue    # 非导出地址 = 假阳性常量；字节保留原样不动
        slots.append((rva, mod, fn))
    slots.sort()
    return slots

def main(dumpdir, outpath, oep_rva, probe):
    dump = bytearray(open(os.path.join(dumpdir, 'dump.bin'), 'rb').read())
    slots = load_slots(dumpdir)
    print('import slots from iatmap: %d' % len(slots))
    ovr = dict(PROBE_OVR) if probe else {}
    unknown = [hex(r) for r, m, f in slots if f == '?']
    if unknown:
        print('WARNING: unresolved-name slots: %s' % unknown)

    pe = struct.unpack_from('<I', dump, 0x3c)[0]
    nsec = struct.unpack_from('<H', dump, pe + 6)[0]
    optsz = struct.unpack_from('<H', dump, pe + 20)[0]
    opt = pe + 24
    dd = opt + 96
    imgsize = struct.unpack_from('<I', dump, opt + 56)[0]

    # 分组：相邻槽(RVA差4)且同模块
    groups = []
    for rva, mod, fn in slots:
        if rva in ovr:
            mod, fn = ovr[rva]
        if groups and groups[-1][2] == mod and groups[-1][0] + 4 * groups[-1][1] == rva:
            groups[-1][1] += 1; groups[-1][3].append(fn)
        else:
            groups.append([rva, 1, mod, [fn]])
    print('IAT: %d groups, %d thunks' % (len(groups), sum(g[1] for g in groups)))

    # 合成 .imp
    newsec_va = align(imgsize, 0x1000)
    buf = bytearray()
    desc_off = len(buf)
    buf += b'\x00' * (20 * (len(groups) + 1))
    name_offs, oft_offs = [], []
    for start, cnt, mod, labels in groups:
        name_offs.append(len(buf))
        buf += os.path.basename(mod).lower().encode('ascii', 'replace') + b'\x00'
        while len(buf) % 4: buf += b'\x00'
        hn, of = [], []
        for lab in labels:
            if lab == '?':
                hn.append(None); of.append(0x80000001)  # 序号1占位(不应发生)
            else:
                hn.append(len(buf)); of.append(None)
                buf += b'\x00\x00' + lab.encode('ascii', 'replace') + b'\x00'
                if len(buf) % 2: buf += b'\x00'
        oft_offs.append(len(buf))
        for k in range(cnt):
            if of[k] is not None:
                buf += struct.pack('<I', of[k])
            else:
                buf += struct.pack('<I', newsec_va + hn[k])
        buf += b'\x00\x00\x00\x00'
    for i, (start, cnt, mod, labels) in enumerate(groups):
        struct.pack_into('<IIIII', buf, desc_off + 20 * i,
                         newsec_va + oft_offs[i], 0, 0, newsec_va + name_offs[i], start)
    newsec_size = align(len(buf), 0x1000)

    out = bytearray(dump) + buf
    out += b'\x00' * (newsec_va + newsec_size - len(out))
    secoff = opt + optsz
    struct.pack_into('<8sIIIIIIHHI', out, secoff + 40 * nsec,
                     b'.imp\x00\x00\x00\x00', newsec_size, newsec_va, newsec_size,
                     newsec_va, 0, 0, 0, 0, 0xC0000040)
    struct.pack_into('<H', out, pe + 6, nsec + 1)
    struct.pack_into('<I', out, opt + 56, newsec_va + newsec_size)
    struct.pack_into('<I', out, opt + 4 + 16, 0x1000)
    for i in range(nsec):
        so = secoff + 40 * i
        vs, va = struct.unpack_from('<II', out, so + 8)
        struct.pack_into('<II', out, so + 16, align(max(vs, 4), 0x1000), va)
        ch = struct.unpack_from('<I', out, so + 36)[0]
        struct.pack_into('<I', out, so + 36, ch | 0x80000000)  # IAT 落在节内，loader 要写
    struct.pack_into('<I', out, opt + 16, oep_rva)
    struct.pack_into('<II', out, dd + 8, newsec_va + desc_off, 20 * (len(groups) + 1))
    struct.pack_into('<II', out, dd + 12 * 8, 0, 0)   # IAT dir
    struct.pack_into('<II', out, dd + 5 * 8, 0, 0)    # reloc
    struct.pack_into('<II', out, dd + 11 * 8, 0, 0)   # bound imports
    open(outpath, 'wb').write(out)
    print('written %s (0x%x bytes), entry RVA=0x%x%s'
          % (outpath, len(out), oep_rva, ' [PROBE]' if probe else ''))

if __name__ == '__main__':
    probe = '--probe' in sys.argv
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    main(args[0], args[1], int(args[2], 0), probe)
