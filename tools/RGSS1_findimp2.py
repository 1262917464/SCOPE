#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""RGSS1_findimp2.py <dumpdir> — 纯静态寻找原版 IMAGE_IMPORT_DESCRIPTOR 链
验证线索（不依赖运行期指针）：
  Name -> "*.dll" 字符串；OFT/FT 均为合法 RVA；OFT 数组条目指向 hint/name（WORD hint + ASCII 名）
  或带 0x80000000 序号位；链以全零描述符结尾。
"""
import struct, sys, os

def main(dd):
    d = open(os.path.join(dd, 'dump.bin'), 'rb').read()
    N = len(d)

    def str_at(rva, maxlen=32):
        if not (0 < rva < N): return None
        e = d.find(b'\0', rva, rva + maxlen)
        if e < 0: return None
        s = d[rva:e]
        if not all(32 <= c < 127 for c in s): return None
        return s.decode('latin1')

    def hn_at(rva):
        """hint/name 条目: WORD hint + ASCII 名（长度>=1）"""
        if not (0 < rva < N - 2): return None
        e = d.find(b'\0', rva + 2, rva + 2 + 40)
        if e < 0: return None
        s = d[rva + 2:e]
        if not s or not all(33 <= c < 127 for c in s): return None
        return s.decode('latin1')

    def check_desc(D):
        oft, ts, fc, nameRva, ft = struct.unpack_from('<IIIII', d, D)
        if not nameRva or not ft or nameRva >= N or ft >= N: return None
        if oft >= N: oft = 0
        nm = str_at(nameRva)
        if not nm or not nm.lower().endswith('.dll'): return None
        th = oft if oft else ft
        n, names, ordn = 0, 0, 0
        while n < 2048:
            t = struct.unpack_from('<I', d, th + 4 * n)[0]
            if t == 0: break
            if t & 0x80000000: ordn += 1
            else:
                hn = hn_at(t)
                if hn: names += 1
                else: return None
            n += 1
        if n == 0 or (names + ordn) < n: return None
        return (nm, n, names, ordn, oft, ft)

    cands = []
    for D in range(0x1000, N - 40, 4):
        c = check_desc(D)
        if not c or c[1] < 3: continue
        # 链完整性
        chain, off = [], D
        while off + 20 <= N and len(chain) < 24:
            c2 = check_desc(off)
            if c2 is None:
                if d[off:off+20] == b'\0' * 20:
                    chain.append(None); break
                break
            chain.append(c2); off += 20
        if len(chain) >= 3 and chain and chain[-1] is None:
            cands.append((D, chain))
    if not cands:
        print(f'{dd}: NO intact chain'); return
    for D, chain in cands[:4]:
        print(f'{dd}: chain @ {D:#x} ({len(chain)-1} descriptors)')
        for i, c in enumerate(chain):
            if c is None: break
            print(f'   {c[0]:14s} thunks={c[1]:3d} (names={c[2]}, ords={c[3]}) OFT={c[4]:#x} FT={c[5]:#x}')
        print(f'   -> importdir RVA={D:#x} size={20*len(chain):#x}')

if __name__ == '__main__':
    for dd in sys.argv[1:]:
        main(dd)
