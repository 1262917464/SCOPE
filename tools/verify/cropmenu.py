# cropmenu.py — f04 左侧菜单窗口: 纯文字子区域统计 + 边框外裁剪
# 文字项 (物品/特殊技能/装备/状态/存档/结束游戏) 在 contents 内,
# 避开窗口边框白线 (x<=22, y>=150) 与光标框 (第一行 y<48 有闪烁相位噪声)
import struct, sys, os

def read_bmp(path):
    data = open(path, 'rb').read()
    off = struct.unpack_from('<I', data, 10)[0]
    w, h = struct.unpack_from('<ii', data, 18)
    h = abs(h)
    stride = w * 4
    return w, h, [data[off + r*stride: off + r*stride + stride] for r in range(h)]

def px(rows, x, y):
    o = x * 4
    return rows[y][o+2], rows[y][o+1], rows[y][o]  # RGB

od, rd = sys.argv[1], sys.argv[2]
W, H, ro = read_bmp(os.path.join(od, 'f04_menu.bmp'))
_, _, rr = read_bmp(os.path.join(rd, 'f04_menu.bmp'))

# 纯文字区域: x 28..160, y 52..146 (第二项"特殊技能"起, 避开光标所在第一行)
X0, X1, Y0, Y1 = 28, 160, 52, 146
def nearwhite(rows):
    n = 0; tot = 0
    for y in range(Y0, Y1):
        for x in range(X0, X1):
            r, g, b = px(rows, x, y)
            if r >= 200 and g >= 200 and b >= 180: n += 1
            tot += 1
    return n, tot
co, to = nearwhite(ro); cr, tr = nearwhite(rr)
print('纯文字区 (%d,%d)-(%d,%d) 近白: orig=%d rebuild=%d 比=%.3f' % (X0, Y0, X1, Y1, co, cr, cr/co))
s = n = big = 0
for y in range(Y0, Y1):
    for x in range(X0, X1):
        a, b = px(ro, x, y), px(rr, x, y)
        d = max(abs(a[i]-b[i]) for i in range(3))
        s += d; n += 1
        if d > 8: big += 1
print('纯文字区 meanΔ=%.1f  >tol=%d/%d (%.1f%%)' % (s/n, big, n, big*100.0/n))
# 另测"正常高亮文字"行 (第一行被选中, 文字应为纯白): x28..90, y 18..46
X0, X1, Y0, Y1 = 28, 90, 18, 46
co, to = nearwhite(ro); cr, tr = nearwhite(rr)
print('选中行近白: orig=%d rebuild=%d 比=%.3f' % (co, cr, cr/co))
