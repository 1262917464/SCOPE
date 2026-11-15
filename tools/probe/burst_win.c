/* burst_win.c — 进图后密集连拍（每 60ms 一帧 ×240 ≈ 14.4s）,
 * 用于窗口 close 动画文字残留对拍。用法同 burst3。 */
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
            char cls[64] = {0};
            GetClassNameA(h, cls, sizeof(cls));
            if (!strcmp(cls, "RGSS Player")) return h;
        }
    }
    return NULL;
}
static DWORD launch(const char *dir)
{
    STARTUPINFOA si; PROCESS_INFORMATION pi; char cmd[512];
    memset(&si, 0, sizeof(si)); si.cb = sizeof(si);
    sprintf(cmd, "%s%c%s", dir, '\\', "Game.exe");
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, dir, &si, &pi)) {
        printf("launch failed %lu\n", GetLastError());
        return 0;
    }
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return pi.dwProcessId;
}
static int shot(HWND h, const char *outdir, int idx)
{
    HDC wdc, mdc; HBITMAP bmp, old; RECT rc; BITMAPINFO bi; void *bits; FILE *f;
    BITMAPFILEHEADER bfh; int w, hh, stride; unsigned char *row; char path[MAX_PATH];
    if (!IsWindow(h)) return 0;
    GetClientRect(h, &rc); w = rc.right; hh = rc.bottom;
    if (w <= 0 || hh <= 0) return 0;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -hh;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    wdc = GetDC(h); mdc = CreateCompatibleDC(wdc);
    bmp = CreateDIBSection(mdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    old = SelectObject(mdc, bmp);
    BitBlt(mdc, 0, 0, w, hh, wdc, 0, 0, SRCCOPY); GdiFlush();
    stride = w * 4;
    sprintf(path, "%s\\f%03d.bmp", outdir, idx);
    f = fopen(path, "wb");
    if (f) {
        bfh.bfType = 0x4D42;
        bfh.bfOffBits = sizeof(bfh) + sizeof(bi.bmiHeader);
        bfh.bfSize = bfh.bfOffBits + stride * hh;
        fwrite(&bfh, sizeof(bfh), 1, f);
        fwrite(&bi.bmiHeader, sizeof(bi.bmiHeader), 1, f);
        for (row = bits; row < (unsigned char *)bits + stride * hh; row += stride)
            fwrite(row, 1, stride, f);
        fclose(f);
    }
    SelectObject(mdc, old); DeleteObject(bmp); DeleteDC(mdc); ReleaseDC(h, wdc);
    return 1;
}
static void tap_key(WORD vk)
{
    keybd_event((BYTE)vk, (BYTE)MapVirtualKeyA(vk, 0), 0, 0);
    Sleep(90);
    keybd_event((BYTE)vk, (BYTE)MapVirtualKeyA(vk, 0), KEYEVENTF_KEYUP, 0);
}
int main(int argc, char **argv)
{
    const char *dir = (argc > 1) ? argv[1] : ".";
    const char *out = (argc > 2) ? argv[2] : "winburst";
    DWORD pid; HWND h; int i, got = 0;
    CreateDirectoryA(out, NULL);
    pid = launch(dir);
    if (!pid) return 1;
    printf("pid=%lu\n", (unsigned long)pid); fflush(stdout);
    for (i = 0; i < 200; i++) { Sleep(100); h = find_game_window(pid); if (h) break; }
    if (!h) { printf("no window\n"); return 1; }
    for (i = 0; i < 10; i++) {
        if (GetForegroundWindow() == h) break;
        keybd_event(VK_MENU, 0, 0, 0);
        SetForegroundWindow(h);
        keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0);
        Sleep(300);
    }
    if (GetForegroundWindow() != h) printf("WARN: not foreground\n");
    SetFocus(h);
    Sleep(6000);
    tap_key(VK_RETURN);          /* 进图 */
    Sleep(3000);
    for (i = 0; i < 240; i++) {
        if (shot(h, out, got)) got++;
        if (!IsWindow(h)) break;
        Sleep(60);
    }
    TerminateProcess(OpenProcess(PROCESS_TERMINATE, FALSE, pid), 0);
    printf("DONE got=%d\n", got); fflush(stdout);
    return 0;
}
