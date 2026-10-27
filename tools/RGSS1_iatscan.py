#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""RGSS1_iatscan.py <dumpdir> [modmaps_dir]
找 dump 里连续的已解析 IAT 槽段（≥3 连续 dword 落在模块区间）。
输出: iatmap.txt（槽 RVA -> 模块!函数 / APPHELP / ?）+ 摘要。
modmaps_dir 默认 = dumpdir（要求 modules.txt/modmaps.txt 同 boot）。
"""
import struct, sys, os

def build(d):
    mods = []
    for ln in open(os.path.join(d, 'modules.txt')):
        b, s, n = ln.rstrip('\n').split('\t')
        mods.append((int(b, 16), int(s, 16), n))
    expmap = {}
    for ln in open(os.path.join(d, 'modmaps.txt')):
        parts = ln.rstrip('\n').split('\t')
        if len(parts) < 3: continue
        mname, rva, fname = parts[0], int(parts[1], 16), parts[2]
        if mname == 'TARGET': continue
        for base, size, name in mods:
            if mname == name:
                key = base + rva
                if key not in expmap:
                    if len(parts) >= 7 and parts[4] == 'FWD':
                        expmap[key] = (name.split('\\')[-1].lower(), 'FWD->%s!%s' % (parts[5], parts[6]))
                    else:
                        expmap[key] = (name.split('\\')[-1].lower(), fname)
                break
    return mods, expmap

def main(dd, md=None):
    md = md or dd
    d = open(os.path.join(dd, 'dump.bin'), 'rb').read()
    mods, expmap = build(md)

    def attr(v):
        for base, size, name in mods:
            if base <= v < base + size:
                nm = name.split('\\')[-1].lower()
                e = expmap.get(v)
                return (nm, e[1] if e else '?')
        return None

    runs, cur = [], []
    for off in range(0x1000, len(d) - 4, 4):
        v = struct.unpack_from('<I', d, off)[0]
        if attr(v):
            cur.append(off)
        else:
            if len(cur) >= 3: runs.append((cur[0], cur[-1] - cur[0] + 4, len(cur)))
            cur = []
    if len(cur) >= 3: runs.append((cur[0], cur[-1] - cur[0] + 4, len(cur)))

    out = open(os.path.join(dd, 'iatmap.txt'), 'w')
    tot = {}
    apphelp_slots = []
    for st, span, n in runs:
        vals = []
        for k in range(n // 4 if span == 4 * (n // 4) else span // 4):
            a = st + 4 * k
            v = struct.unpack_from('<I', d, a)[0]
            m = attr(v)
            mod, fn = m if m else ('?', '?')
            tot[mod] = tot.get(mod, 0) + 1
            if mod == 'apphelp.dll':
                apphelp_slots.append((a, v))
            vals.append('%s!%s' % (mod, fn))
        out.write('RUN rva=%#x..%#x n=%d\n' % (st, st + span, span // 4))
        for k, s in enumerate(vals):
            a = st + 4 * k
            out.write('  %#010x  %08X  %s\n' % (a, struct.unpack_from('<I', d, a)[0], s))
    out.close()
    print('== %s: %d runs ==' % (dd, len(runs)))
    for m, c in sorted(tot.items(), key=lambda x: -x[1]):
        print('   %-16s %d' % (m, c))
    print('   apphelp-polluted slots: %d' % len(apphelp_slots))

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None)
