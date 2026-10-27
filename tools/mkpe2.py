#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
mkpe2.py — 生成【可运行】的脱壳 PE（标准导入表 + OEP 入口）
用法: python mkpe2.py <dumpdir> <out.dll> <oep_rva> [--probe]
                        [--slot RVA=mod.dll!FuncName ...] [--self SUBSTR]

--probe: (202E 旧管线) 把 apphelp 无法识别的两个槽(0xFF2DC/0xFF324)改为导入
         rgssprobe.dll 的探针函数, 供运行期参数采集。
--slot:  显式指定某 IAT 槽的导入 (RVA 十六进制)。给出 --slot 时忽略旧管线
         的 FINAL_OVR/PROBE_OVR 硬编码, 以 --slot 为准。
--self:  追加"自身模块名子串"(run 扫描时排除指向自身导出的指针段)。
         202E 默认排除 'RGSS20'/'unpacker'; RGSS300 需 --self RGSS3。
"""
import struct, sys, os

def align(x, a): return (x + a - 1) & ~(a - 1)

def main(dumpdir, outpath, oep_rva, probe=False, slots=None, self_substrs=()):
    slots = slots or {}
    dump = bytearray(open(os.path.join(dumpdir, 'dump.bin'), 'rb').read())
    mods = []
    for ln in open(os.path.join(dumpdir, 'modules.txt')):
        b, s, n = ln.rstrip('\n').split('\t')
        mods.append((int(b, 16), int(s, 16), n))
    expmap = {}
    for ln in open(os.path.join(dumpdir, 'modmaps.txt')):
        parts = ln.rstrip('\n').split('\t')
        if len(parts) < 3: continue
        mname, rva, fname = parts[0], int(parts[1], 16), parts[2]
        if mname == 'TARGET': continue
        for base, size, name in mods:
            if mname == name:
                key = base + rva
                if key not in expmap:
                    if len(parts) >= 7 and parts[4] == 'FWD':
                        tm = parts[5] + '.dll'; fn = parts[6]
                        expmap[key] = (tm, int(fn[1:]) if fn.startswith('#') else fn)
                    else:
                        expmap[key] = (name, fname)
                break

    pe = struct.unpack_from('<I', dump, 0x3c)[0]
    nsec = struct.unpack_from('<H', dump, pe + 6)[0]
    optsz = struct.unpack_from('<H', dump, pe + 20)[0]
    opt = pe + 24
    ddoff = opt + 96
    imgsize = struct.unpack_from('<I', dump, opt + 56)[0]

    # ---- 1. IAT 扫描: ≥2 连续同模块 → 确定导入区 → 区域内单槽回收 ----
    runs = []
    cs = cm = None; cc = 0
    for off in range(0x1000, len(dump) - 4, 4):
        v = struct.unpack_from('<I', dump, off)[0]
        m = expmap.get(v)
        if m:
            if cm == m[0] and cc: cc += 1
            else:
                if cc >= 2: runs.append((cs, cc, cm))
                cs, cm, cc = off, m[0], 1
        else:
            if cc >= 2: runs.append((cs, cc, cm))
            cs = cm = None; cc = 0
    if cc >= 2: runs.append((cs, cc, cm))
    excl = ['RGSS20', 'unpacker'] + list(self_substrs)
    core = [r for r in runs if r[0] < 0x200000
            and not any(s in r[2] for s in excl)]
    merged = []
    for r in core:
        if merged:
            p = merged[-1]
            if r[2] == p[2] and r[0] - (p[0] + 4 * p[1]) <= 32:
                p[1] = (r[0] + 4 * r[1] - p[0]) // 4
                continue
        merged.append([r[0], r[1], r[2]])
    if merged:
        lo = min(r[0] for r in merged) - 8
        hi = max(r[0] + 4 * r[1] for r in merged) + 8
        covered = set()
        for st, n, _ in merged:
            for k in range(n): covered.add(st + 4 * k)
        for off in range(lo, hi, 4):
            if off in covered: continue
            v = struct.unpack_from('<I', dump, off)[0]
            m = expmap.get(v)
            if m: merged.append([off, 1, m[0]])
        merged.sort(key=lambda r: r[0])

    # ---- 2. 展开成带标签的槽位流, 应用覆盖, 重组描述符组 ----
    # apphelp 污染槽定名(202E M2 证据, RGSS300 经反汇编互证):
    #   0x1A236C = IsWindowVisible  (单参, bool→WS_VISIBLE 位; = 202E 的 7014DEA0)
    #   0x1A2390 = MessageBoxW      (4 参 hwnd/text/caption/uType; = 202E 的 701500B0)
    FINAL_OVR = {0xFF2DC: ('user32.dll', 'MessageBoxW'),
                 0xFF324: ('user32.dll', 'IsWindowVisible')}
    PROBE_OVR = {0xFF2DC: ('rgssprobe.dll', 'probe_a'), 0xFF324: ('rgssprobe.dll', 'probe_b')}
    if slots:
        OVR = slots                     # RGSS3 管线: 显式 --slot 覆盖
    else:
        OVR = PROBE_OVR if probe else FINAL_OVR   # 202E 旧管线行为保持不变
    slots = []      # [(addr, (module, name_or_int))]
    unresolved = 0
    for st, cnt, mod in merged:
        for k in range(cnt):
            a = st + 4 * k
            if a in OVR:
                slots.append([a, OVR[a]]); continue
            v = struct.unpack_from('<I', dump, a)[0]
            nm = expmap.get(v)
            if nm is None:
                nm = ('user32.dll', 0)      # 兜底: 序号0 占位(不应发生)
                unresolved += 1
            slots.append([a, nm])
    groups = []     # [start, count, module, [labels...]]
    for a, nm in slots:
        if groups and groups[-1][2] == nm[0] and groups[-1][0] + 4 * groups[-1][1] == a:
            groups[-1][1] += 1; groups[-1][3].append(nm[1])
        else:
            groups.append([a, 1, nm[0], [nm[1]]])
    if unresolved: print('WARNING: %d unresolved slots (ordinal-0 stub)' % unresolved)
    print('IAT: %d groups, %d thunks' % (len(groups), sum(g[1] for g in groups)))

    # ---- 3. 合成 .imp3 ----
    newsec_va = align(imgsize, 0x1000)
    buf = bytearray()
    desc_off = len(buf)
    buf += b'\x00' * (20 * (len(groups) + 1))
    oft_offs, name_offs = [], []
    for start, cnt, mod, labels in groups:
        name_offs.append(len(buf))
        buf += os.path.basename(mod).lower().encode('ascii', 'replace') + b'\x00'
        while len(buf) % 4: buf += b'\x00'
        hn_entries, ord_flags = [], []
        for lab in labels:
            if isinstance(lab, int):
                hn_entries.append(None); ord_flags.append(0x80000000 | lab)
            else:
                hn_entries.append(len(buf)); ord_flags.append(None)
                buf += b'\x00\x00' + lab.encode('ascii', 'replace') + b'\x00'
                if len(buf) % 2: buf += b'\x00'
        oft_offs.append(len(buf))
        for k in range(cnt):
            if ord_flags[k] is not None:
                buf += struct.pack('<I', ord_flags[k])
            else:
                buf += struct.pack('<I', newsec_va + hn_entries[k])
        buf += b'\x00\x00\x00\x00'          # OFT NULL 终止
    for i, (start, cnt, mod, labels) in enumerate(groups):
        struct.pack_into('<IIIII', buf, desc_off + 20 * i,
                         newsec_va + oft_offs[i], 0, 0,
                         newsec_va + name_offs[i], start)
    newsec_size = align(len(buf), 0x1000)

    out = bytearray(dump) + buf
    out += b'\x00' * (newsec_va + newsec_size - len(out))
    secoff = opt + optsz
    struct.pack_into('<8sIIIIIIHHI', out, secoff + 40 * nsec,
                     b'.imp3\x00\x00\x00\x00', newsec_size, newsec_va, newsec_size,
                     newsec_va, 0, 0, 0, 0, 0xC0000040)
    struct.pack_into('<H', out, pe + 6, nsec + 1)
    struct.pack_into('<I', out, opt + 56, newsec_va + newsec_size)
    struct.pack_into('<I', out, opt + 4 + 16, 0x1000)
    for i in range(nsec):
        so = secoff + 40 * i
        vs, va = struct.unpack_from('<II', out, so + 8)
        struct.pack_into('<II', out, so + 16, align(max(vs, 4), 0x1000), va)
    struct.pack_into('<I', out, opt + 16, oep_rva)
    struct.pack_into('<II', out, ddoff + 8, newsec_va + desc_off, 20 * (len(groups) + 1))
    struct.pack_into('<II', out, ddoff + 12 * 8, 0, 0)
    struct.pack_into('<II', out, ddoff + 5 * 8, 0, 0)
    struct.pack_into('<II', out, ddoff + 11 * 8, 0, 0)
    open(outpath, 'wb').write(out)
    print('written %s (0x%x bytes), entry RVA=0x%x%s'
          % (outpath, len(out), oep_rva, ' [PROBE]' if probe else ''))

if __name__ == '__main__':
    probe = '--probe' in sys.argv
    slot_args = {}
    self_substrs = []
    rest = []
    i = 1
    while i < len(sys.argv):
        a = sys.argv[i]
        if a == '--slot':
            i += 1
            k, v = sys.argv[i].split('=', 1)
            mod, name = v.split('!', 1)
            slot_args[int(k, 16)] = (mod, name)
        elif a == '--self':
            i += 1
            self_substrs.append(sys.argv[i])
        elif not a.startswith('--'):
            rest.append(a)
        i += 1
    main(rest[0], rest[1], int(rest[2], 0), probe, slot_args, self_substrs)
