# framediff.py — 原版 vs 重建 截图逐像素对比
# 用法: py tools/framediff.py <orig_dir> <rebuild_dir> [tol] [outdir]
# 读取两组 f00..f05 BMP (32bpp)，输出:
#   - 每对帧的差异统计 (差异像素比例 / 平均通道差 / 最大差)
#   - 差异像素包围盒 + 差异最集中的 32x32 块 top10
#   - 放大 8 倍的差异可视化 BMP (diff_fXX.bmp) 供肉眼复核
import struct, sys, os

def read_bmp(path):
    with open(path, 'rb') as f:
        data = f.read()
    if data[:2] != b'BM':
        raise ValueError('not bmp: ' + path)
    off = struct.unpack_from('<I', data, 10)[0]
    w, h = struct.unpack_from('<ii', data, 18)
    bpp = struct.unpack_from('<H', data, 28)[0]
    if bpp != 32:
        raise ValueError('%s: bpp=%d, need 32' % (path, bpp))
    top_down = h > 0
    h = abs(h)
    stride = w * 4
    px = bytearray(w * h * 4)
    for row in range(h):
        src = off + row * stride
        dst = row * stride if top_down else (h - 1 - row) * stride
        px[dst:dst + stride] = data[src:src + stride]
    # BGRA -> 索引访问按 (y*w+x)*4: B,G,R,A
    return w, h, px

def diff_pair(a, b, tol):
    w, h, pa = a
    w2, h2, pb = b
    if (w, h) != (w2, h2):
        return None
    n = w * h
    diffcount = 0
    sumdiff = 0
    maxdiff = 0
    minx, miny, maxx, maxy = w, h, -1, -1
    dmap = bytearray(n)  # 每像素 0/255
    for i in range(n):
        o = i * 4
        db = abs(pa[o] - pb[o]); dg = abs(pa[o+1] - pb[o+1]); dr = abs(pa[o+2] - pb[o+2])
        d = db if db >= dg else dg
        if dr > d: d = dr
        if d > tol:
            diffcount += 1
            dmap[i] = 255
            sumdiff += d
            if d > maxdiff: maxdiff = d
            x = i % w; y = i // w
            if x < minx: minx = x
            if x > maxx: maxx = x
            if y < miny: miny = y
            if y > maxy: maxy = y
    # 32x32 块热力
    bw, bh = w // 32, h // 32
    blocks = []
    for by in range(bh):
        for bx in range(bw):
            c = 0
            for y in range(by*32, by*32+32):
                base = y * w
                row = dmap[base+bx*32: base+bx*32+32]
                c += row.count(255)
            if c:
                blocks.append((c, bx, by))
    blocks.sort(reverse=True)
    return dict(count=diffcount, ratio=diffcount/n, meandiff=(sumdiff/diffcount if diffcount else 0),
                maxdiff=maxdiff, bbox=(minx, miny, maxx, maxy), blocks=blocks[:10], dmap=dmap)

def write_diff_bmp(w, h, pa, pb, dmap, path):
    stride = w * 4
    body = bytearray(stride * h)
    for i in range(w*h):
        o = i * 4
        if dmap[i]:
            # 差异像素: 品红, 亮度按差异程度
            db = abs(pa[o] - pb[o]); dg = abs(pa[o+1] - pb[o+1]); dr = abs(pa[o+2] - pb[o+2])
            d = max(db, dg, dr)
            v = 100 + min(155, d)
            body[o] = v       # B
            body[o+1] = 0     # G
            body[o+2] = v     # R
        else:
            g = (pa[o] + pa[o+1] + pa[o+2]) // 12  # 原图压暗做背景
            body[o] = g; body[o+1] = g; body[o+2] = g
    bfh = struct.pack('<2sIHHI', b'BM', 54 + len(body), 0, 0, 54)
    bih = struct.pack('<IiiHHIIiiII', 40, w, h, 1, 32, 0, len(body), 0, 0, 0, 0)
    with open(path, 'wb') as f:
        f.write(bfh + bih + body)

def main():
    orig_d, reb_d = sys.argv[1], sys.argv[2]
    tol = int(sys.argv[3]) if len(sys.argv) > 3 else 8
    outdir = sys.argv[4] if len(sys.argv) > 4 else os.path.join(reb_d, 'diff_out')
    os.makedirs(outdir, exist_ok=True)
    names = ['f00_title.bmp','f01_map.bmp','f02_down.bmp','f03_left.bmp','f04_menu.bmp','f05_closed.bmp']
    report = []
    report.append('RGSS202E 1:1 帧对比  tol=%d (通道差>%d 记为差异像素)' % (tol, tol))
    report.append('orig=%s  rebuild=%s' % (orig_d, reb_d))
    report.append('')
    for name in names:
        po, pr = os.path.join(orig_d, name), os.path.join(reb_d, name)
        if not (os.path.exists(po) and os.path.exists(pr)):
            report.append('%s: MISSING (%s%s)' % (name, 'orig ' if not os.path.exists(po) else '', 'rebuild' if not os.path.exists(pr) else ''))
            continue
        A = read_bmp(po); B = read_bmp(pr)
        if A[0] != B[0] or A[1] != B[1]:
            report.append('%s: SIZE MISMATCH orig=%dx%d rebuild=%dx%d' % (name, A[0], A[1], B[0], B[1]))
            continue
        r = diff_pair(A, B, tol)
        report.append('%s: diff=%.2f%%  meanΔ=%.1f  maxΔ=%d  bbox=%s' % (
            name, r['ratio']*100, r['meandiff'], r['maxdiff'],
            r['bbox'] if r['count'] else 'none'))
        for c, bx, by in r['blocks']:
            report.append('    block (%3d,%3d)-(%3d,%3d): %d px' % (bx*32, by*32, bx*32+31, by*32+31, c))
        if r['count']:
            write_diff_bmp(A[0], A[1], A[2], B[2], r['dmap'], os.path.join(outdir, 'diff_' + name.replace('.bmp', '.bmp')))
    out = '\n'.join(report)
    print(out)
    with open(os.path.join(outdir, 'report.txt'), 'w', encoding='utf-8') as f:
        f.write(out + '\n')

if __name__ == '__main__':
    main()
