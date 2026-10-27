# textcheck.py — B47 定点复核: 文字渲染像素对质 (原版 vs 重建)
# 用法: py tools/verify/textcheck.py <orig_dir> <rebuild_dir>
# 检查1: M5-j 基准点 (标题命令窗 "讀取存檔" 禁用项)
#   (210,340) 文字主体 / (210,341) 阴影内部 / (210,344) 阴影边缘
# 检查2: M5-k 菜单文字区 (x18..175, y15..160) 近白像素比 (>=200/200/180)
# 检查3: 同区域逐像素 mean|diff|
import struct, sys, os

def read_bmp(path):
    data = open(path, 'rb').read()
    off = struct.unpack_from('<I', data, 10)[0]
    w, h = struct.unpack_from('<ii', data, 18)
    h = abs(h)
    stride = w * 4
    rows = []
    for r in range(h):
        src = off + r * stride
        rows.append(data[src:src + stride])
    return w, h, rows

def px(rows, x, y):
    o = x * 4
    b, g, r = rows[y][o], rows[y][o+1], rows[y][o+2]
    return r, g, b

od, rd = sys.argv[1], sys.argv[2]
for name in ('f00_title.bmp', 'f04_menu.bmp'):
    po, pr = os.path.join(od, name), os.path.join(rd, name)
    if not (os.path.exists(po) and os.path.exists(pr)):
        print(name, 'MISSING'); continue
    W, H, ro = read_bmp(po)
    _, _, rr = read_bmp(pr)
    print('==', name, '==')
    if name == 'f00_title.bmp':
        for label, (x, y) in (('文字主体(210,340)', (210, 340)), ('阴影内部(210,341)', (210, 341)),
                              ('阴影边缘(210,344)', (210, 344)), ('正常文字(210,314)', (210, 314))):
            print('  %s  orig=%s  rebuild=%s' % (label, px(ro, x, y), px(rr, x, y)))
        # 阴影内部 5x5 均值 (y 339..343, x 208..212)
        for label, (x0, y0) in (('阴影内部5x5均值', (208, 339)),):
            so = [0,0,0]; sr = [0,0,0]; n = 0
            for dx in range(5):
                for dy in range(5):
                    a = px(ro, x0+dx, y0+dy); b = px(rr, x0+dx, y0+dy)
                    for i in range(3): so[i]+=a[i]; sr[i]+=b[i]
                    n += 1
            print('  %s  orig=(%d,%d,%d)  rebuild=(%d,%d,%d)' % (label, *(v//n for v in so), *(v//n for v in sr)))
    if name == 'f04_menu.bmp':
        for tag, rows in (('orig', ro), ('rebuild', rr)):
            nw = 0; tot = 0
            for y in range(15, 160):
                for x in range(18, 175):
                    r, g, b = px(rows, x, y)
                    if r >= 200 and g >= 200 and b >= 180: nw += 1
                    tot += 1
            print('  近白像素 %s: %d (%.3f 相对基准)' % (tag, nw, 0))
        # 相对比例
        def cnt(rows):
            nw = 0
            for y in range(15, 160):
                for x in range(18, 175):
                    r, g, b = px(rows, x, y)
                    if r >= 200 and g >= 200 and b >= 180: nw += 1
            return nw
        co, cr = cnt(ro), cnt(rr)
        print('  近白像素比 rebuild/orig = %.3f  (%d vs %d)' % (cr/co, cr, co))
        # 逐像素 mean|diff| 同区域
        s = 0; n = 0; big = 0
        for y in range(15, 160):
            for x in range(18, 175):
                a = px(ro, x, y); b = px(rr, x, y)
                d = max(abs(a[i]-b[i]) for i in range(3))
                s += d; n += 1
                if d > 8: big += 1
        print('  区域 meanΔ=%.1f  >tol 像素=%d/%d (%.1f%%)' % (s/n, big, n, big*100.0/n))
