#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""parse_rvdata.py v2 — 解析 RGSS Scripts.rvdata
磁盘结构: [section_id, name, zlib_deflated_bytes] × N
运行时引导: $RGSS_SCRIPTS.each { |s| s[3,0] = Zlib::Inflate.inflate(s[2]) }
用法: python parse_rvdata.py <Scripts.rvdata> [outdir]
"""
import sys, os, zlib


class MR:
    def __init__(self, d):
        self.d = d; self.p = 0; self.sym = []; self.obj = []

    def b(self):
        c = self.d[self.p]; self.p += 1; return c

    def long(self):
        c = self.b()
        if c == 0: return 0
        if 5 <= c < 128: return c - 5
        if c < 128:
            n = 0
            for i in range(c): n |= self.b() << (8 * i)
            return n
        cnt = 256 - c
        n = 0
        for i in range(cnt): n |= self.b() << (8 * i)
        return n - (1 << (8 * cnt))

    def sbytes(self):
        n = self.long()
        s = self.d[self.p:self.p + n]; self.p += n
        return s

    def val(self):
        t = chr(self.b())
        if t == '0': return None
        if t == 'T': return True
        if t == 'F': return False
        if t == 'i': return self.long()
        if t == ':':
            n = self.long(); s = self.d[self.p:self.p + n]; self.p += n
            s = s.decode('latin1'); self.sym.append(s); return s
        if t == ';': return self.sym[self.long()]
        if t == '"':
            s = self.sbytes(); self.obj.append(s); return s
        if t == '[':
            n = self.long(); a = []; self.obj.append(a)
            for _ in range(n): a.append(self.val())
            return a
        if t == '{':
            n = self.long(); h = {}; self.obj.append(h)
            for _ in range(n):
                k = self.val(); h[k] = self.val()
            return h
        if t == 'o':
            cls = self.val(); n = self.long(); o = {'__class__': cls}
            self.obj.append(o)
            for _ in range(n): o[self.val()] = self.val()
            return o
        if t == 'u':
            cls = self.val(); body = self.sbytes()
            o = {'__class__': cls, '__user__': body}; self.obj.append(o); return o
        if t == 'l':
            s = self.sbytes(); self.obj.append(s); return s
        if t == 'I':
            o = self.val(); n = self.long()
            for _ in range(n):
                k = self.val(); v = self.val()
                if isinstance(o, dict): o[k] = v
            return o
        if t == '@': return self.obj[self.long()]
        if t == 'f': return float(self.sbytes().decode('latin1'))
        if t == 'S':
            cls = self.val(); n = self.long(); st = {'__class__': cls}
            self.obj.append(st)
            for _ in range(n): st[self.val()] = self.val()
            return st
        if t == 'C': return self.val()
        if t == '/': return None
        raise ValueError('unknown %r at %x' % (t, self.p - 1))


def main(path, outdir=None):
    data = open(path, 'rb').read()
    print('file: %d bytes' % len(data))
    r = MR(data)
    print('marshal: %d.%d' % (r.b(), r.b()))
    obj = r.val()
    print('sections: %d' % len(obj))
    if outdir: os.makedirs(outdir, exist_ok=True)
    total = 0
    rows = []
    for i, e in enumerate(obj):
        if not isinstance(e, list) or len(e) < 3:
            rows.append((i, '?', 0, 0, 'BAD')); continue
        sid, nm, z = e[0], e[1], e[2]
        try:
            code = zlib.decompress(z) if z else b''
            st = 'OK'
        except Exception as ex:
            code = b''; st = 'INFLATE_FAIL:%s' % ex
        total += len(code)
        name = nm.decode('utf-8', 'replace') if isinstance(nm, bytes) else str(nm)
        rows.append((i, name, len(z or b''), len(code), st))
        if outdir:
            safe = ''.join(c if (c.isalnum() or c in '._-') else '_' for c in name)[:50]
            open(os.path.join(outdir, '%03d_%s.rb' % (i, safe or 'noname')), 'wb').write(code)
    for i, name, zs, cs, st in rows:
        print('[%03d] %-42s z=%-7d src=%-7d %s' % (i, name, zs, cs, st))
    print('\ntotal inflated: %d bytes' % total)
    bad = [r for r in rows if r[4] != 'OK']
    print('failed sections: %d' % len(bad))

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None)
