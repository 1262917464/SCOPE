/* ftprobe.c — 文件转场逐帧截屏 (M5-n 验证)
 * 用法: ftprobe.exe <项目目录> <输出目录> < shots [interval_ms]
 * 流程: 设 RGSS_TEST_EVAL (freeze + 延迟线程触发文件转场) → 拉起 Game.exe →
 *       窗口出现后定时整屏截图。Game.exe 需为重建 DLL 版 (依赖 RGSS_TEST_EVAL)。
 */
#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <stdio.h>
#include <string.h>

static HWND find_game_window(DWORD pid)
{
    HWND h = NULL;
    while ((h = FindWindowEx(NULL, h, NULL, NULL)) != NULL) {
        DWORD wpid = 0;
        GetWindowThreadProcessId(h, &wpid);
        if (wpid == pid && IsWindowVisible(h)) {
            char cls[64] = {0}, txt[128] = {0};
            GetClassNameA(h, cls, sizeof(cls));
            GetWindowTextA(h, txt, sizeof(txt));
            if (!strcmp(cls, "RGSS Player") || strstr(txt, "Project")) return h;
        }
    }
    return NULL;
}

static void shot(HWND h, const char *outdir, const char *name)
{
    HDC wdc, mdc;
    HBITMAP bmp, old;
    RECT rc;
    BITMAPINFO bi;
    void *bits;
    FILE *f;
    BITMAPFILEHEADER bfh;
    int w, hh, stride;
    unsigned char *row;
    char path[MAX_PATH];

    if (!IsWindow(h)) return;
    GetClientRect(h, &rc);
    w = rc.right; hh = rc.bottom;
    if (w <= 0 || hh <= 0) return;

    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -hh;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    wdc = GetDC(h);
    mdc = CreateCompatibleDC(wdc);
    bmp = CreateDIBSection(mdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    old = SelectObject(mdc, bmp);
    BitBlt(mdc, 0, 0, w, hh, wdc, 0, 0, SRCCOPY);
    GdiFlush();

    stride = w * 4;
    sprintf(path, "%s\\%s", outdir, name);
    f = fopen(path, "wb");
    if (f) {
        bfh.bfType = 0x4D42;
        bfh.bfOffBits = sizeof(bfh) + sizeof(bi.bmiHeader);
        bfh.bfSize = bfh.bfOffBits + stride * hh;
        bfh.bfReserved1 = bfh.bfReserved2 = 0;
        fwrite(&bfh, sizeof(bfh), 1, f);
        fwrite(&bi.bmiHeader, sizeof(bi.bmiHeader), 1, f);
        for (row = (unsigned char *)bits; row < (unsigned char *)bits + stride * hh; row += stride)
            fwrite(row, 1, stride, f);
        fclose(f);
    }
    SelectObject(mdc, old);
    DeleteObject(bmp);
    DeleteDC(mdc);
    ReleaseDC(h, wdc);
}

int main(int argc, char **argv)
{
    const char *dir = argv[1];
    const char *out = argv[2];
    int shots = argc > 3 ? atoi(argv[3]) : 150;
    int interval = argc > 4 ? atoi(argv[4]) : 100;

    /* 6 秒后 freeze, 再 500ms 后文件转场 30 帧 (遮罩 TestMask, vague=60) */
    SetEnvironmentVariableA("RGSS_TEST_EVAL",
        "File.open('eval_ran.txt','w') { |f| f.puts 'y' }; unless defined?(Graphics.orig_upd); module Graphics; class << self; "
        "alias_method :orig_upd, :update; def update; @c ||= 0; @c += 1; "
        "if @c == 10; Graphics.freeze; sp = Sprite.new; "
        "sp.bitmap = Bitmap.new(544,416); "
        "sp.bitmap.fill_rect(0,0,544,416,Color.new(255,0,0)); end; "
        "if @c == 25; Graphics.transition(30, 'Graphics/System/TestMask', 60); end; "
        "orig_upd; end; end; end; end");

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    char cmd[512];
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    sprintf(cmd, "\"%s\\Game.exe\"", dir);
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, dir, &si, &pi)) {
        printf("launch fail %lu\n", GetLastError());
        return 1;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    HWND h = NULL;
    for (int i = 0; i < 200 && !h; i++) { Sleep(100); h = find_game_window(pi.dwProcessId); }
    if (!h) { printf("no window\n"); return 1; }
    SetForegroundWindow(h);
    /* 转场约在 +6.5s 开始, 从 +6.0s 起以 interval 截 40 帧 */
    Sleep(1000);
    for (int i = 0; i < shots; i++) {
        char name[64];
        sprintf(name, "ft%02d.bmp", i);
        shot(h, out, name);
        Sleep(interval);
    }
    PostMessageA(h, WM_CLOSE, 0, 0);
    return 0;
}
