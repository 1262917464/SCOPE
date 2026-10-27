#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""find_init_fns.py — 从 full_disasm.txt 定位每个 RGSS 类/模块的 Init 注册函数"""
import re

p = r'E:\rgss2-rebuild\build\full_disasm.txt'
lines = open(p, encoding='latin1').read().splitlines()
entries = []
pat_line = re.compile(r'^\s*([0-9a-f]+):\s')
pat_push = re.compile(r'push\s+0x(1010[0-9a-f]{4})')
for ln in lines:
    m = pat_line.match(ln)
    if m:
        entries.append((int(m.group(1), 16), ln))

rgss = {0x103fd8: 'Bitmap', 0x10406c: 'Color', 0x1041b8: 'Font', 0x10485c: 'Plane',
        0x1048a0: 'Rect', 0x1049a0: 'Sprite', 0x1049d0: 'Table', 0x104a50: 'Tilemap',
        0x104a68: 'Tone', 0x104a8c: 'Viewport', 0x104b74: 'Window',
        0x104288: 'Graphics', 0x104304: 'Input', 0x103de8: 'Audio'}
pat_int3 = re.compile(r'\scc\s+int3')

print('entries: %d' % len(entries))
for i, (addr, ln) in enumerate(entries):
    m = pat_push.search(ln)
    if not m:
        continue
    key = int(m.group(1), 16) - 0x10000000
    if key not in rgss:
        continue
    fstart = 0
    for j in range(i, max(0, i - 800), -1):
        if pat_int3.search(entries[j][1]):
            fstart = entries[j + 1][0]
            break
    print('%-9s push@%06x  init_fn=%06x' % (rgss[key], addr - 0x1000000,
                                             fstart - 0x1000000))
