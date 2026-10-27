#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""M3.2 取证：DLL 内字符串 vs 官方 Ruby 1.9.2p0 源码字面量双向差集。
用法: python tools/rgss3_strdiff.py
输出: build/rgss3_strdiff.txt (Enterbrain 候选增补 + 源码侧候选删除)
"""
import re, os, sys, collections

SRC = 'third_party/ruby-1.9.2-p0'
DUMP = 'unpacked/RGSS3_300_pristine/dump.bin'
OUT = 'build/rgss3_strdiff.txt'

def main():
    d = open(DUMP, 'rb').read()

    # ---- 官方源码字面量 ----
    pat = re.compile(rb'"((?:[^"\\\n]|\\.){6,300})"')
    def dec(bs):
        out = bytearray(); i = 0
        while i < len(bs):
            c = bs[i]
            if c == 0x5c and i + 1 < len(bs):
                n = bs[i+1]
                if 0x30 <= n <= 0x37:
                    m = re.match(rb'[0-7]{1,3}', bs[i+1:i+4]); out.append(int(m.group(0), 8) & 0xff); i += 1 + len(m.group(0)); continue
                if n == 0x78:
                    m = re.match(rb'[0-9a-fA-F]{1,2}', bs[i+2:i+4])
                    if m: out.append(int(m.group(0), 16)); i += 2 + len(m.group(0)); continue
                mp = {0x6e:10, 0x74:9, 0x72:13, 0x5c:0x5c, 0x22:0x22, 0x27:0x27, 0x30:0, 0x61:7, 0x62:8, 0x66:12, 0x76:11}
                out.append(mp.get(n, n)); i += 2; continue
            out.append(c); i += 1
        return bytes(out)

    lit = set(); nfiles = 0
    for root, dirs, fs in os.walk(SRC):
        for f in fs:
            if not f.endswith(('.c', '.h', '.y', '.rl')): continue
            nfiles += 1
            try: data = open(os.path.join(root, f), 'rb').read()
            except Exception: continue
            for m in pat.finditer(data):
                try:
                    s = dec(m.group(1))
                    if len(s) >= 6 and all(32 <= c < 127 or c in (9, 10, 13) for c in s):
                        lit.add(s.decode('latin1'))
                except Exception: pass
    print('source files: %d  distinct ascii literals>=6: %d' % (nfiles, len(lit)))

    # ---- DLL 字符串 ----
    dll = [(m.start(), m.group(0).decode('latin1')) for m in re.finditer(rb'[\x20-\x7e]{6,200}', d)]
    dllset = set(s for _, s in dll)
    insrc = sum(1 for _, s in dll if s in lit)
    print('dll ascii runs>=6: %d  in-source: %d' % (len(dll), insrc))

    def region(o):
        if o < 0x4700: return 'engine'
        if 0x10D000 <= o < 0x111000: return 'nxcsx'
        if 0x4700 <= o < 0x1A2000: return 'code'
        if o < 0x2AE000: return 'rdata'
        return 'data'

    # DLL-only 候选（排除已知引擎/第三方关键词）
    skipkw = ('.cpp', '.dll', '.exe', 'NxDraw', 'rgss', 'RGSS', 'Enterbrain',
              '.rvdata', 'Bitmap', 'Sprite', 'Viewport', 'Tilemap', 'theora',
              'Theora', 'ogg', 'OGG', 'zlib', 'png', 'libpng', 'jpeg', 'JPEG')
    cand = collections.defaultdict(list)
    for o, s in dll:
        if s in lit: continue
        if any(k in s for k in skipkw): continue
        cand[region(o)].append((o, s))

    lines = []
    lines.append('=== Enterbrain 候选增补（DLL 有 / 官方源码无）===')
    for r in ('code', 'rdata', 'data', 'engine', 'nxcsx'):
        rows = cand.get(r, [])
        lines.append('-- %s (%d) --' % (r, len(rows)))
        for o, s in rows[:40]:
            lines.append('  %06X  %s' % (o, s))

    # 源码侧候选删除：官方字面量在 DLL 里找不到（排除短/常见）
    missing = sorted(s for s in lit if s not in dllset and len(s) >= 12)
    lines.append('')
    lines.append('=== 官方源码有 / DLL 无（候选被裁/未编入，>=12 字节，%d 条）===' % len(missing))
    for s in missing[:80]:
        lines.append('  %s' % s)
    lines.append('  ... 共 %d 条' % len(missing))

    open(OUT, 'w', encoding='utf-8').write('\n'.join(lines))
    print('written %s' % OUT)

if __name__ == '__main__':
    main()
