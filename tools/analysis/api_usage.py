#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""api_usage.py — 从解压的游戏脚本统计 RGSS API 实际使用频率
用法: python api_usage.py <scripts_dir>
"""
import sys, os, re
from collections import Counter, defaultdict

# RGSS 类名（来自 328 条映射表）
CLASSES = ['Bitmap', 'Sprite', 'Plane', 'Tilemap', 'Window', 'Viewport', 'Font',
           'Color', 'Tone', 'Rect', 'Table', 'Graphics', 'Input', 'Audio']

def main(d):
    files = sorted(f for f in os.listdir(d) if f.endswith('.rb'))
    cls_use = Counter()
    meth_use = defaultdict(Counter)
    per_file = defaultdict(set)
    total_lines = 0

    # 类名.方法 形式
    pat_cls = re.compile(r'\b(' + '|'.join(CLASSES) + r')\s*\.\s*([A-Za-z_][A-Za-z0-9_?!=]*)')
    # 类名.new
    pat_new = re.compile(r'\b(' + '|'.join(CLASSES) + r')\s*\.\s*new\b')
    # 局部变量/实例变量上的方法调用无法静态归因，跳过

    for f in files:
        path = os.path.join(d, f)
        try:
            src = open(path, encoding='utf-8', errors='replace').read()
        except Exception:
            continue
        total_lines += src.count('\n')
        for m in pat_cls.finditer(src):
            cls, meth = m.group(1), m.group(2)
            cls_use[cls] += 1
            meth_use[cls][meth] += 1
            per_file[f].add(cls)
        for m in pat_new.finditer(src):
            cls = m.group(1)
            cls_use[cls] += 1
            meth_use[cls]['new'] += 1

    print('=== 扫描 %d 个文件, %d 行 Ruby ===' % (len(files), total_lines))
    print('\n=== 类使用频次 ===')
    for cls, n in cls_use.most_common():
        print('%-10s %5d 次, 涉及 %d 个文件' % (cls, n,
              sum(1 for f in per_file if cls in per_file[f])))
    print('\n=== 各类方法使用 TOP ===')
    for cls, n in cls_use.most_common():
        tops = ', '.join('%s×%d' % (k, v) for k, v in meth_use[cls].most_common(12))
        print('%-10s %s' % (cls, tops))

    # 未被使用的类
    unused = [c for c in CLASSES if c not in cls_use]
    if unused:
        print('\n=== 脚本中未直接使用的类 ===')
        print(', '.join(unused))

if __name__ == '__main__':
    main(sys.argv[1])
