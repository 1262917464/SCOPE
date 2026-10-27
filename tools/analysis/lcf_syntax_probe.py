"""穷举验证 LCF 头部语法 —— 不靠猜，靠自洽性。

对每个候选 body 起始偏移 + 每种 BER 变体，尝试把正文按
(ber len, byte code, payload) 完整平铺到 EOF。
能精确平铺且 code 分布合理的，就是正确语法。
"""
import sys


def ber_be(blob, p):
    v = 0
    while p < len(blob):
        b = blob[p]; p += 1
        v = (v << 7) + (b & 0x7f)
        if not (b & 0x80):
            break
    else:
        return None, p
    return v, p


def ber_le(blob, p):
    v = 0; shift = 0
    while p < len(blob):
        b = blob[p]; p += 1
        v |= (b & 0x7f) << shift
        shift += 7
        if not (b & 0x80):
            break
    else:
        return None, p
    return v, p


def tile(blob, start, berfn):
    """尝试从 start 平铺到 EOF。返回 (chunks, ok)。"""
    p = start
    chunks = []
    while p < len(blob):
        ln, p2 = berfn(blob, p)
        if ln is None or p2 >= len(blob):
            return chunks, False
        if p2 >= len(blob):
            return chunks, False
        code = blob[p2]
        body = p2 + 1
        end = body + ln
        if end > len(blob):
            return chunks, False
        chunks.append((code, ln, body))
        p = end
        if len(chunks) > 500:
            return chunks, False
    return chunks, p == len(blob)


def probe(path):
    d = open(path, 'rb').read()
    n = d[0]
    magic = d[1:1+n].decode('latin1')
    print("=" * 72)
    print(path, len(d), "magic=", magic)
    best = []
    for start in range(1 + n, min(1 + n + 24, len(d))):
        for name, fn in (('BE', ber_be), ('LE', ber_le)):
            ch, ok = tile(d, start, fn)
            if ok and ch:
                best.append((start, name, ch))
    for start, name, ch in best:
        codes = [c for c, _, _ in ch]
        print("  start=%2d BER=%-2s chunks=%3d codes[:12]=%s" %
              (start, name, len(ch), codes[:12]))
    if not best:
        print("  !! no exact tiling found")
    return best


for p in sys.argv[1:]:
    probe(p)
