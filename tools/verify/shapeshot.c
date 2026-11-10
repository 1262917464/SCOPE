/* shapeshot.c — shape oracle 截图驱动: go<k> → 等 done<k> → 截图 → go<k+1>
 * 用法: shapeshot.exe <项目目录> <输出目录> */
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
static DWORD launch(const char *dir)
{
    STARTUPINFOA si; PROCESS_INFORMATION pi; char cmd[512];
    memset(&si, 0, sizeof(si)); si.cb = sizeof(si);
    sprintf(cmd, "%s%c%s", dir, '\\', "Game.exe");
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, dir, &si, &pi)) return 0;
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return pi.dwProcessId;
}
static void shot(HWND h, const char *outdir, const char *name)
{
    HDC wdc, mdc; HBITMAP bmp, old; RECT rc; BITMAPINFO bi; void *bits; FILE *f;
    BITMAPFILEHEADER bfh; int w, hh, stride; unsigned char *row; char path[MAX_PATH];
    if (!IsWindow(h)) return;
    GetClientRect(h, &rc); w = rc.right; hh = rc.bottom;
    if (w <= 0 || hh <= 0) return;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -hh;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    wdc = GetDC(h); mdc = CreateCompatibleDC(wdc);
    bmp = CreateDIBSection(mdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    old = SelectObject(mdc, bmp);
    BitBlt(mdc, 0, 0, w, hh, wdc, 0, 0, SRCCOPY); GdiFlush();
    stride = w * 4;
    sprintf(path, "%s%c%s", outdir, '\\', name);
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
}
static void tap_key(WORD vk)
{
    keybd_event((BYTE)vk, (BYTE)MapVirtualKeyA(vk, 0), 0, 0);
    Sleep(90);
    keybd_event((BYTE)vk, (BYTE)MapVirtualKeyA(vk, 0), KEYEVENTF_KEYUP, 0);
}
static void wfile(const char *dir, const char *name)
{
    char path[MAX_PATH];
    sprintf(path, "%s%c%s", dir, '\\', name);
    FILE *f = fopen(path, "w");
    if (f) fclose(f);
}
static int rfile(const char *dir, const char *name)
{
    char path[MAX_PATH];
    sprintf(path, "%s%c%s", dir, '\\', name);
    FILE *f = fopen(path, "rb");
    if (f) { fclose(f); return 1; }
    return 0;
}
int main(int argc, char **argv)
{
    const char *dir = (argc > 1) ? argv[1] : ".";
    const char *out = (argc > 2) ? argv[2] : "shape_shots";
    DWORD pid; HWND h; int i, k;
    char nm[64];
    CreateDirectoryA(out, NULL);
    pid = launch(dir);
    if (!pid) { printf("launch failed\n"); return 1; }
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
    SetFocus(h);
    Sleep(4000);
    tap_key(VK_RETURN);   /* 标题 → 新游戏 → 进地图 */
    for (i = 0; i < 100; i++) { Sleep(100); if (rfile(dir, "ready.txt")) break; }
    if (i >= 100) { printf("no ready\n"); return 1; }
    wfile(dir, "go0.txt");
    for (k = 0; k < 16; k++) {
        sprintf(nm, "done%d.txt", k);
        for (i = 0; i < 100; i++) { Sleep(100); if (rfile(dir, nm)) break; }
        if (i >= 100) { printf("timeout waiting done%d\n", k); break; }
        Sleep(400);
        sprintf(nm, "s%02d.bmp", k);
        shot(h, out, nm);
        printf("shot %s\n", nm); fflush(stdout);
        if (k < 15) {
            sprintf(nm, "go%d.txt", k + 1);
            wfile(dir, nm);
        }
    }
    TerminateProcess(OpenProcess(PROCESS_TERMINATE, FALSE, pid), 0);
    printf("DONE\n");
    return 0;
}
