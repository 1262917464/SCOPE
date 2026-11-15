# star_run.py — 稳健采集: 重试多次，验证前台+进图(黑度)，成功即出 4 帧
# 用法: star_run.py <项目目录> <输出目录>
import ctypes, os, subprocess, sys, time, struct

u = ctypes.windll.user32
PWSHOT = r'E:\rgss2-rebuild\tools\probe\pwshot.exe'

def find_win(pid):
    h = None
    while True:
        h = u.FindWindowExA(None, h, None, None)
        if not h:
            return None
        c = ctypes.create_string_buffer(64)
        u.GetClassNameA(h, c, 64)
        w = ctypes.c_ulong()
        u.GetWindowThreadProcessId(h, ctypes.byref(w))
        if w.value == pid and c.value == b'RGSS Player':
            return h

def foreground(h):
    for _ in range(8):
        u.keybd_event(0x12, 0, 0, 0)
        u.SetForegroundWindow(h)
        u.keybd_event(0x12, 0, 2, 0)
        time.sleep(0.3)
        if u.GetForegroundWindow() == h:
            return True
    return False

def blackness(path):
    d = open(path, 'rb').read()
    px = d[struct.unpack_from('<I', d, 10)[0]:]
    n = 0
    for i in range(0, len(px), 4):
        if px[i] == 0 and px[i+1] == 0 and px[i+2] == 0:
            n += 1
    return n * 1000000 // (len(px) // 4)

def tap(vk):
    sc = u.MapVirtualKeyA(vk, 0)
    u.keybd_event(vk, sc, 0, 0)
    time.sleep(0.1)
    u.keybd_event(vk, sc, 2, 0)

def attempt(proj, outdir, idx):
    tmp = os.path.join(outdir, 'probe.bmp')
    p = subprocess.Popen([os.path.join(proj, 'Game.exe')], cwd=proj)
    try:
        h = None
        for _ in range(150):
            time.sleep(0.1)
            h = find_win(p.pid)
            if h:
                break
        if not h or not foreground(h):
            return 'no-window'
        time.sleep(6)
        tap(0x0D)                      # Enter: 标题→地图
        time.sleep(3)
        if p.poll() is not None:
            return 'died-after-enter code=%s' % p.poll()
        # 走进测试簇: LEFT ×6, DOWN ×2
        for _ in range(6):
            tap(0x25)
            time.sleep(0.25)
        for _ in range(2):
            tap(0x28)
            time.sleep(0.25)
        time.sleep(1)
        if p.poll() is not None:
            return 'died-after-walk code=%s' % p.poll()
        subprocess.run([PWSHOT, tmp], capture_output=True)
        b = blackness(tmp)
        print('  probe blackness=%d/1000000' % b)
        if b > 500000:                 # 仍在标题(黑)
            return 'still-title'
        # 已进图: 采 4 帧 ×500ms
        for k in range(4):
            subprocess.run([PWSHOT, os.path.join(outdir, 'f%d.bmp' % k)],
                           capture_output=True)
            time.sleep(0.5)
        return 'ok'
    finally:
        p.kill()

def main():
    proj, outdir = sys.argv[1], sys.argv[2]
    os.makedirs(outdir, exist_ok=True)
    for i in range(6):
        r = attempt(proj, outdir, i)
        print('attempt %d: %s' % (i, r))
        if r == 'ok':
            print('SUCCESS')
            return
    print('FAILED all attempts')

main()
