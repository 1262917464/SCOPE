#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""gen_def.py — 从原版 exports.txt 生成 .def 文件, 保证导出表 1:1"""
import sys, os

def main(exports_txt, out_def):
    rows = []
    for ln in open(exports_txt, encoding='utf-8'):
        p = ln.rstrip('\n').split('\t')
        if len(p) < 4 or p[0] != 'TARGET':
            continue
        rva = int(p[1], 16)
        name = p[2]
        ordinal = int(p[3])
        rows.append((ordinal, name, rva))
    rows.sort()
    with open(out_def, 'w', encoding='ascii', errors='replace') as f:
        f.write('LIBRARY RGSS202E\nEXPORTS\n')
        for ordinal, name, rva in rows:
            f.write('    %s @%d\n' % (name, ordinal))
    print('wrote %d exports to %s' % (len(rows), out_def))
    # 同时输出 C 侧检查清单
    with open(out_def + '.txt', 'w', encoding='utf-8') as f:
        for ordinal, name, rva in rows:
            f.write('%-28s @%-3d rva=0x%06x\n' % (name, ordinal, rva))

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
