# pyburst.py — Python 驱动连拍（PrintWindow，抗焦点问题）
# 用法: pyburst.py <项目目录> <输出目录>
import ctypes, os, subprocess, sys, time

u = ctypes.windll.user32

def find_win(pid):
    h = None
    while True:
        h = u.FindWindowExA(None, h, None, None)
        if not h:
            return None
        c = ctypes.create_string_buffer(64)
        u.GetClassNameA(h, c, 64)
        wpid = ctypes.c_ulong()
        u.GetWindowThreadProcessId(h, ctypes.byref(wpid))
        if wpid.value == pid and u.IsWindowVisible(h) and c.value == b'RGSS Player':
            return h

def foreground(h):
    for _ in range(10):
        if u.GetForegroundWindow() == h:
            break
        u.SetForegroundWindow(h)
        time.sleep(0.3)
    return u.GetForegroundWindow() == h

def enter_key():
    u.keybd_event(0x0D, 0, 0, 0)
    time.sleep(0.1)
    u.keybd_event(0x0D, 0, 2, 0)

def main():
    proj, outdir = sys.argv[1], sys.argv[2]
    pwshot = r'E:\rgss2-rebuild\tools\probe\pwshot.exe'
    os.makedirs(outdir, exist_ok=True)
    p = subprocess.Popen([os.path.join(proj, 'Game.exe')], cwd=proj)
    print('pid=%d' % p.pid)
    h = None
    for _ in range(200):
        time.sleep(0.1)
        h = find_win(p.pid)
        if h:
            break
    if not h:
        print('no window')
        sys.exit(1)
    print('hwnd ok, fg=%s' % foreground(h))
    time.sleep(6)
    enter_key()
    time.sleep(3)
    got = 0
    for i in range(16):
        h = find_win(p.pid) or h
        if not h or not u.IsWindow(h):
            print('window gone at', i, 'poll=', p.poll())
            hw = None
            while True:
                hw = u.FindWindowExA(None, hw, None, None)
                if not hw:
                    break
                w = ctypes.c_ulong()
                u.GetWindowThreadProcessId(hw, ctypes.byref(w))
                if w.value == p.pid:
                    c = ctypes.create_string_buffer(64)
                    u.GetClassNameA(hw, c, 64)
                    print('   win:', hw, c.value, u.IsWindowVisible(hw))
            break
        if u.GetForegroundWindow() != h:
            foreground(h)
        out = os.path.join(outdir, 'b%02d.bmp' % i)
        r = subprocess.run([pwshot, out], capture_output=True,
                           encoding='utf-8', errors='replace')
        if r.stdout and 'saved' in r.stdout:
            got += 1
        else:
            print(i, (r.stdout or '').strip(), (r.stderr or '').strip())
        time.sleep(0.5)
    p.kill()
    print('DONE got=%d/16' % got)

if __name__ == '__main__':
    main()
