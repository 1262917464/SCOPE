#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""decode_rbdefs.py v2 — 精确解码 RGSS Ruby 方法注册
基于实测序列 (cdecl 逆序 push):
    push fn        ; rb_define_method(klass, name, fn, argc)
    push name
    push argc
    push klass     ; 寄存器(ecx/edx/eax), 形如 'push ecx'
    call rb_define_method
=> 紧邻 call 前的 4 条 push 依次为 klass,name,argc,fn? 实测顺序为:
   1000c945 push 0x1000c480   <- fn
   1000c94a push 0x10103fcc   <- name("initialize")
   1000c94f push ecx          <- klass
   1000c950 call
   (argc 缺省时视为 -1, 见其它样例: 1000c95b push 0x1 / push fn / push name / push klass / call)
   因此顺序为: [可选 argc] , fn, name, klass, call
"""
import re, bisect

DIS = r'E:\rgss2-rebuild\build\full_disasm.txt'
DUMP = r'E:\rgss2-rebuild\unpacked\RGSS202E_pristine\dump.bin'
OUT = r'E:\rgss2-rebuild\docs\assets\rgss_ruby_api_map.tsv'

RB_DEFM = 0x10021070
RB_SING = 0x10021250
RB_ALLOC = 0x10027310

# 起点已由 find_init_fns 实测确认; 上界 = 下一个 Init 起点(避免串入相邻函数)
INITS = [
    ('Audio',    0x10003B60, 0x10006060),
    ('Bitmap',   0x10006060, 0x100068C0),
    ('Color',    0x100068C0, 0x100075D0),
    ('Font',     0x100075D0, 0x100084C0),
    ('Graphics', 0x100084C0, 0x100087B0),
    ('Input',    0x100087B0, 0x1000C150),
    ('Plane',    0x1000C150, 0x1000C910),
    ('Rect',     0x1000C910, 0x1000E260),
    ('Sprite',   0x1000E260, 0x1000EF40),
    ('Table',    0x1000EF40, 0x1000FB50),
    ('Tilemap',  0x1000FB50, 0x100101A0),
    ('Tone',     0x100101A0, 0x10011090),
    ('Viewport', 0x10011090, 0x100122E0),
    ('Window',   0x100122E0, 0x10014000),
]

dump = open(DUMP, 'rb').read()
def cstr(va):
    rva = va - 0x10000000
    if not (0 <= rva < len(dump)): return None
    e = dump.find(b'\x00', rva)
    if e < 0 or e - rva > 64: return None
    s = dump[rva:e]
    try: return s.decode('latin1')
    except Exception: return None

pat = re.compile(r'^\s*([0-9a-f]+):\s+[0-9a-f ]+\t(\S+)\s*(.*)$')
ins = []
for ln in open(DIS, encoding='latin1'):
    m = pat.match(ln)
    if m:
        ins.append((int(m.group(1), 16), m.group(2), m.group(3).strip()))
addrs = [i[0] for i in ins]

def is_code(v):
    return v is not None and 0x10001000 <= v < 0x100FF000

def is_name(v):
    return v is not None and 0x10100000 <= v < 0x1017FFFF

rows = []
for cls, lo, hi in INITS:
    i = bisect.bisect_left(addrs, lo)
    win = []      # 最近若干条 push 的立即数(None=寄存器)
    while i < len(ins) and ins[i][0] < hi:
        a, mn, ops = ins[i]
        if mn == 'push':
            if ops.startswith('0x'):
                win.append(int(ops, 16))
            elif re.match(r'^-?\d+$', ops):
                win.append(int(ops))
            else:
                win.append(None)
            win = win[-8:]
            i += 1
            continue
        if mn == 'call':
            try: tgt = int(ops, 16)
            except ValueError: tgt = None
            if tgt in (RB_DEFM, RB_SING, RB_ALLOC):
                api = {RB_DEFM: 'define_method', RB_SING: 'define_singleton_method',
                       RB_ALLOC: 'alloc_func'}[tgt]
                if api == 'alloc_func':
                    fn = next((v for v in reversed(win) if is_code(v)), None)
                    rows.append((cls, api, '', '', fn))
                else:
                    # 从后往前: klass(寄存器或值) 之前依次是 name, fn, [argc]
                    seq = list(win)
                    # 去掉末尾的 klass(可能是 None 或 code/class VA)
                    if seq and (seq[-1] is None or is_code(seq[-1]) or is_name(seq[-1])):
                        seq = seq[:-1]
                    name = fn = None; argc = -1
                    for v in reversed(seq):
                        if is_name(v) and name is None:
                            name = cstr(v)
                        elif is_code(v) and fn is None:
                            fn = v
                        elif v is not None and -2 <= v <= 20 and fn is not None:
                            argc = v
                            break
                    rows.append((cls, api, name or '?', argc, fn))
                win = []
                i += 1
                continue
        # 其它指令: 保留窗口(跨函数调用可能混入), 但限制长度
        i += 1

with open(OUT, 'w', encoding='utf-8') as f:
    f.write('# class\tapi\tmethod\targc\tfn_va\tfn_rva\n')
    for cls, api, name, argc, fn in rows:
        f.write('%s\t%s\t%s\t%s\t%s\t%s\n' % (
            cls, api, name, argc,
            '0x%08x' % fn if fn else '', '0x%06x' % (fn - 0x10000000) if fn else ''))

from collections import Counter
print('rows:', len(rows))
for k, v in sorted(Counter(r[0] for r in rows).items()):
    miss = sum(1 for r in rows if r[0] == k and not r[4])
    print('  %-9s %d (missing fn: %d)' % (k, v, miss))
