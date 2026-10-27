#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""api_usage2.py — 增强版 RGSS API 使用统计
追踪 @ivar = Klass.new / local = Klass.new，再统计 var.method 调用
"""
import sys, os, re
from collections import Counter, defaultdict

CLASSES = ['Bitmap', 'Sprite', 'Plane', 'Tilemap', 'Window', 'Viewport', 'Font',
           'Color', 'Tone', 'Rect', 'Table', 'Graphics', 'Input', 'Audio']
CLS_RE = '|'.join(CLASSES)

def main(d):
    files = sorted(f for f in os.listdir(d) if f.endswith('.rb'))
    # 类级统计
    cls_use = Counter()
    meth_use = defaultdict(Counter)
    # 变量 → 类（文件内作用域，简化处理：全局累积）
    var_type = defaultdict(set)
    var_meth = defaultdict(Counter)

    # @ivar = Klass.new / var = Klass.new
    pat_assign = re.compile(r'(@?[A-Za-z_][A-Za-z0-9_]*)\s*=\s*(' + CLS_RE + r')\s*\.\s*new\b')
    # Klass.method
    pat_cls = re.compile(r'\b(' + CLS_RE + r')\s*\.\s*([A-Za-z_][A-Za-z0-9_?!=]*)')
    # var.method
    pat_var = re.compile(r'\b(@?[A-Za-z_][A-Za-z0-9_]*)\s*\.\s*([A-Za-z_][A-Za-z0-9_?!=]*)')

    total_lines = 0
    for f in files:
        src = open(os.path.join(d, f), encoding='utf-8', errors='replace').read()
        total_lines += src.count('\n')
        for m in pat_assign.finditer(src):
            var_type[m.group(1)].add(m.group(2))
            cls_use[m.group(2)] += 1
            meth_use[m.group(2)]['new'] += 1
        for m in pat_cls.finditer(src):
            cls_use[m.group(1)] += 1
            meth_use[m.group(1)][m.group(2)] += 1
        for m in pat_var.finditer(src):
            var_meth[m.group(1)][m.group(2)] += 1

    # 把变量调用归因到类（仅当变量只对应一个类时）
    for var, types in var_type.items():
        if len(types) == 1:
            cls = next(iter(types))
            for meth, n in var_meth.get(var, {}).items():
                if meth not in ('new',):
                    meth_use[cls][meth] += n
                    cls_use[cls] += n

    print('=== %d 文件 / %d 行 ===' % (len(files), total_lines))
    print('\n=== 类使用总频次（含变量归因） ===')
    for cls, n in cls_use.most_common():
        print('%-10s %6d' % (cls, n))
    print('\n=== 各类方法 TOP 20 ===')
    for cls, _ in cls_use.most_common():
        tops = ', '.join('%s×%d' % (k, v) for k, v in meth_use[cls].most_common(20))
        print('\n%-10s %s' % (cls, tops))
    # 未出现的 RGSS API（对比 328 表）
    print('\n=== 未被脚本使用的 API 类 ===')
    print(', '.join(c for c in CLASSES if c not in cls_use))

if __name__ == '__main__':
    main(sys.argv[1])
