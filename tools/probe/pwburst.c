/* pwburst.c — PrintWindow 连拍（抗最小化）: pwburst.exe <项目目录> <输出目录> */
#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <stdio.h>
#include <string.h>
static HWND find_game_window(DWORD pid)
{
    HWND h = NULL;
    while ((h = FindWindowEx(NULL, h, NULL, NULL)) != NULL) {
        DWORD wpid = 0; char cls[64] = {0};
        GetWindowThreadProcessId(h, &wpid);
        GetClassNameA(h, cls, sizeof(cls));
        if (wpid == pid && IsWindowVisible(h) && !strcmp(cls, "RGSS Player")) return h;
    }
    return NULL;
}
static void foreground(HWND h)
{
    for (int i = 0; i < 10 && GetForegroundWindow() != h; i++) {
        keybd_event(VK_MENU, 0, 0, 0);
        SetForegroundWindow(h);
        keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0);
        Sleep(300);
    }
    ShowWindow(h, SW_RESTORE);
    SetFocus(h);
}
static DWORD launch(const char *dir)
{
    STARTUPINFOA si; PROCESS_INFORMATION pi; char cmd[512];
    memset(&si, 0, sizeof(si)); si.cb = sizeof(si);
    sprintf(cmd, "%s%c%s", dir, 92, "Game.exe");
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, dir, &si, &pi)) return 0;
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return pi.dwProcessId;
}
static int shot(HWND h, const char *outdir, const char *name)
{
    HDC wdc, mdc; HBITMAP bmp, old; RECT rc; BITMAPINFO bi; void *bits; FILE *f;
    BITMAPFILEHEADER bfh; int w, hh, stride; unsigned char *row; char path[MAX_PATH];
    GetClientRect(h, &rc); w = rc.right; hh = rc.bottom;
    if (w <= 0 || hh <= 0) return 0;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -hh;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    wdc = GetDC(h); mdc = CreateCompatibleDC(wdc);
    bmp = CreateDIBSection(mdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    old = SelectObject(mdc, bmp);
    BOOL ok = PrintWindow(h, mdc, 2); GdiFlush();
    stride = w * 4;
    sprintf(path, "%s\%s", outdir, name);
    f = fopen(path, "wb");
    if (f) {
        bfh.bfType = 0x4D42; bfh.bfOffBits = sizeof(bfh) + sizeof(bi.bmiHeader);
        bfh.bfSize = bfh.bfOffBits + stride * hh;
        fwrite(&bfh, sizeof(bfh), 1, f); fwrite(&bi.bmiHeader, sizeof(bi.bmiHeader), 1, f);
        for (row = bits; row < (unsigned char *)bits + stride * hh; row += stride)
            fwrite(row, 1, stride, f);
        fclose(f);
    }
    SelectObject(mdc, old); DeleteObject(bmp); DeleteDC(mdc); ReleaseDC(h, wdc);
    return ok ? 1 : 0;
}
int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    const char *out = argc > 2 ? argv[2] : "pwburst";
    DWORD pid; HWND h; int i, got = 0;
    CreateDirectoryA(out, NULL);
    pid = launch(dir);
    if (!pid) { printf("launch failed\n"); return 1; }
    printf("pid=%lu\n", (unsigned long)pid); fflush(stdout);
    for (i = 0; i < 200; i++) { Sleep(100); h = find_game_window(pid); if (h) break; }
    if (!h) { printf("no window\n"); return 1; }
    printf("hwnd ok\n"); fflush(stdout);
    foreground(h);
    Sleep(6000);
    keybd_event(VK_RETURN, 0, 0, 0); Sleep(90); keybd_event(VK_RETURN, 0, KEYEVENTF_KEYUP, 0);
    Sleep(3000);
    for (i = 0; i < 16; i++) {
        char nm[64];
        if (!IsWindow(h)) break;
        if (GetForegroundWindow() != h) foreground(h);
        sprintf(nm, "b%02d.bmp", i);
        got += shot(h, out, nm);
        Sleep(500);
    }
    TerminateProcess(OpenProcess(PROCESS_TERMINATE, FALSE, pid), 0);
    printf("DONE got=%d/16\n", got); fflush(stdout);
    return 0;
}
