/* burst.c — 进图后连拍（每 500ms 一帧 ×12），用于水面动画相位裁决
 * 用法: burst.exe <项目目录> <输出目录>  */
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
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, dir, &si, &pi)) {
        printf("launch failed %lu\n", GetLastError());
        return 0;
    }
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
int main(int argc, char **argv)
{
    const char *dir = (argc > 1) ? argv[1] : ".";
    const char *out = (argc > 2) ? argv[2] : "burst";
    DWORD pid; HWND h; int i;
    CreateDirectoryA(out, NULL);
    pid = launch(dir);
    if (!pid) return 1;
    printf("pid=%lu\n", (unsigned long)pid); fflush(stdout);
    for (i = 0; i < 200; i++) { Sleep(100); h = find_game_window(pid); if (h) break; }
    if (!h) { printf("no window\n"); return 1; }
    printf("hwnd ok\n"); fflush(stdout);
    /* 前台化重试: SetForegroundWindow 可能静默失败, 用 ALT 技巧反复尝试 */
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
    tap_key(VK_RETURN);          /* 进图，与 difftest 同时刻 */
    Sleep(3000);                  /* 转场彻底结束后再开始连拍 */
    for (i = 0; i < 16; i++) {
        char nm[64];
        sprintf(nm, "b%02d.bmp", i);
        shot(h, out, nm);
        if (!IsWindow(h)) break;
        Sleep(500);               /* ≈30 帧 = 水面动画一步 */
    }
    TerminateProcess(OpenProcess(PROCESS_TERMINATE, FALSE, pid), 0);
    printf("DONE\n"); fflush(stdout);
    return 0;
}
