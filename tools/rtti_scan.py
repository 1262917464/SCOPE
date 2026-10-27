#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""rtti_scan.py — 离线解析 MSVC RTTI: 类名清单 + vftable 地址映射
用法: python rtti_scan.py <dump.bin> [out_classes.txt]
原理(32位):
  TypeDescriptor: { vptr, spare, ".?AV<name>\0" }
  CompleteObjectLocator: { sig=0, offset, cdOffset, pTypeDescriptor, pClassDescriptor }
  vftable[-1] -> COL;  COL+12 -> TD
"""
import struct, sys, re

def main(path, out):
    d = open(path, 'rb').read()
    BASE = 0x10000000

    # 1. TypeDescriptor: 找 ".?AV" / ".?AU" 名字串
    tds = []   # (td_rva, name)
    for m in re.finditer(rb'\.\?A[UV][ -~]{1,200}?\x00', d):
        off = m.start()
        # TD 名字在 TD+8; 校验 TD+0 的 vptr 指向 dump 内(rough sanity)
        vptr = struct.unpack_from('<I', d, off - 8)[0]
        if 0x10000000 <= vptr < 0x10000000 + len(d):
            tds.append((off - 8, m.group()[:-1].decode('latin1')))
    print('TypeDescriptors: %d' % len(tds))
    td_rva_set = {t[0] for t in tds}

    # 2. COL(x86): COL+12 的 pTypeDescriptor 是 VA; COL = 引用处-12; sig==0
    td_va_set = {0x10000000 + r for r in td_rva_set}
    td_by_va = {0x10000000 + r: n for r, n in tds}
    td_to_cols = {}
    col_va_set = set()
    for i in range(0x1000, len(d) - 4, 4):
        v = struct.unpack_from('<I', d, i)[0]
        if v in td_va_set:
            col = i - 12
            if col < 0x1000: continue
            sig, off_, cd, ptd, pcd = struct.unpack_from('<IIIII', d, col)
            if sig == 0 and ptd == v and off_ < 0x100 and cd < 0x100 \
               and 0x10000000 <= pcd < 0x10000000 + len(d):
                td_to_cols.setdefault(v, []).append(0x10000000 + col)
                col_va_set.add(0x10000000 + col)
    print('COLs: %d' % len(col_va_set))

    # 3. vftable: vftable[-1] 指向 COL 的 VA → vft 从 x+4 开始
    lines = []
    for i in range(0x1000, len(d) - 4, 4):
        v = struct.unpack_from('<I', d, i)[0]
        if v in col_va_set:
            col = v - 0x10000000
            tdva = struct.unpack_from('<I', d, col + 12)[0]
            name = td_by_va.get(tdva, '?')
            # 统计 vftable 大小: 连续指向代码段的 dword
            vs = 0
            j = i + 4
            while j + 4 <= len(d):
                fv = struct.unpack_from('<I', d, j)[0]
                if 0x10000000 <= fv < 0x10000000 + 0xFF000:  # 代码段
                    vs += 1; j += 4
                else: break
            lines.append((i + 4, name, vs, len(td_to_cols.get(tdva, []))))
    lines.sort()
    print('vftables: %d' % len(lines))
    with open(out, 'w', encoding='utf-8') as f:
        f.write('# vftable_RVA\tclass\tvirtual_count\tCOL_count_for_class\n')
        for rva, name, vs, ncols in lines:
            f.write('%06X\t%s\t%d\t%d\n' % (rva, name, vs, ncols))
    # 类名去重清单
    names = sorted(set(n for _, n in tds))
    with open(out + '.names', 'w', encoding='utf-8') as f:
        for n in names: f.write(n + '\n')
    print('unique classes: %d' % len(names))
    print('sample:', names[:15])

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else 'classes.txt')
