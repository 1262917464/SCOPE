/* blinkprobe.c — 定时截屏测原版光标闪烁周期
 * 用法: blinkprobe.exe <项目目录> <次数> <间隔ms>
 */
#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <stdio.h>
#include <string.h>

static DWORD launch(const char *dir)
{
    STARTUPINFOA si; PROCESS_INFORMATION pi; char cmd[512];
    memset(&si,0,sizeof si); si.cb = sizeof si;
    sprintf(cmd, "\"%s\\Game.exe\"", dir);
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, dir, &si, &pi)) return 0;
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return pi.dwProcessId;
}

static HWND find_game_window(DWORD pid)
{
    HWND h = NULL;
    while ((h = FindWindowEx(NULL, h, NULL, NULL)) != NULL) {
        DWORD wpid = 0; GetWindowThreadProcessId(h, &wpid);
        if (wpid == pid && IsWindowVisible(h)) {
            char cls[64] = {0};
            GetClassNameA(h, cls, sizeof cls);
            if (!strcmp(cls, "RGSS Player")) return h;
        }
    }
    return NULL;
}

/* 区域平均亮度 */
static double region_mean(HWND h, int x, int y, int w, int hh)
{
    HDC wdc = GetDC(h);
    BITMAPINFO bi; void *bits = NULL;
    memset(&bi,0,sizeof bi);
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -hh;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    HBITMAP bmp = CreateDIBSection(wdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    HDC mdc = CreateCompatibleDC(wdc);
    HGDIOBJ old = SelectObject(mdc, bmp);
    BitBlt(mdc, 0, 0, w, hh, wdc, x, y, SRCCOPY);
    GdiFlush();
    long sum = 0; int n = w * hh;
    const unsigned char *p = (const unsigned char *)bits;
    for (int i = 0; i < n; i++)
        sum += (p[i*4+0] + p[i*4+1] + p[i*4+2]) / 3;
    SelectObject(mdc, old); DeleteObject(bmp); DeleteDC(mdc); ReleaseDC(h, wdc);
    return n ? (double)sum / n : 0.0;
}

int main(int argc, char **argv)
{
    const char *dir = argv[1];
    int shots = argc > 2 ? atoi(argv[2]) : 40;
    int interval = argc > 3 ? atoi(argv[3]) : 60;
    DWORD pid = launch(dir);
    if (!pid) { printf("launch fail\n"); return 1; }
    HWND h = NULL;
    for (int i = 0; i < 200 && !h; i++) { Sleep(100); h = find_game_window(pid); }
    if (!h) { printf("no window\n"); return 1; }
    SetForegroundWindow(h); Sleep(300);
    Sleep(7000);   /* 等标题画面稳定 */

    /* 光标区: 窗口(186,288) + contents(16,16) = (202,304) 尺寸约 140x24 */
    for (int i = 0; i < shots; i++) {
        double m = region_mean(h, 202, 304, 140, 24);
        printf("t=%4dms mean=%.2f\n", i * interval, m);
        Sleep(interval);
    }
    PostMessageA(h, WM_CLOSE, 0, 0);
    return 0;
}
