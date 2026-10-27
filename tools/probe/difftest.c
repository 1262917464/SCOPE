/* difftest.c — 1:1 对照截图采集器
 * 拉起 <dir>\Game.exe，等窗口，按固定脚本注入按键，
 * 在固定相对时刻截图。原版/重建各跑一次，产物交给 framediff.py 对比。
 *
 * 用法: difftest.exe <项目目录> <输出目录>
 * 时序（自窗口出现起，毫秒，与 keytest.c 一致以便对齐）:
 *   T0      窗口出现
 *   +6000   shot f00_title.bmp
 *   Enter x1 → +3000  shot f01_map        (进场后；原版含 transition，重建直接切)
 *   Down x3 (每击间隔 500) → +1500  shot f02_down
 *   Left x3 → +1500  shot f03_left
 *   Esc x1 (开菜单) → +2500  shot f04_menu
 *   Esc x1 (关菜单) → +1500  shot f05_closed
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

static DWORD launch(const char *dir)
{
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    char cmd[512];
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    sprintf(cmd, "\"%s\\Game.exe\"", dir);
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, dir, &si, &pi)) {
        printf("CreateProcess failed: %lu\n", GetLastError());
        return 0;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return pi.dwProcessId;
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

    if (!IsWindow(h)) { printf("  shot skipped (no window): %s\n", name); return; }
    GetClientRect(h, &rc);
    w = rc.right; hh = rc.bottom;
    if (w <= 0 || hh <= 0) { printf("  bad client rect\n"); return; }

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
        printf("  saved %s (%dx%d)\n", name, w, hh);
    }
    SelectObject(mdc, old);
    DeleteObject(bmp);
    DeleteDC(mdc);
    ReleaseDC(h, wdc);
}

static void tap_key(WORD vk)
{
    keybd_event((BYTE)vk, (BYTE)MapVirtualKeyA(vk, 0), 0, 0);
    Sleep(90);
    keybd_event((BYTE)vk, (BYTE)MapVirtualKeyA(vk, 0), KEYEVENTF_KEYUP, 0);
}

/* 重复按键，每次后等 gap 毫秒 */
static void tap_n(WORD vk, int n, int gap, HWND h)
{
    int i;
    for (i = 0; i < n; i++) {
        if (!IsWindow(h)) return;
        tap_key(vk);
        Sleep(gap);
    }
}

static int alive(HWND h, DWORD pid)
{
    if (IsWindow(h)) return 1;
    HANDLE ph = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
    DWORD code = 0;
    if (ph) { GetExitCodeProcess(ph, &code); CloseHandle(ph); }
    printf("WINDOW GONE, exitcode=%lu%s\n", code, code == STILL_ACTIVE ? " (proc alive)" : "");
    return 0;
}

int main(int argc, char **argv)
{
    const char *dir = (argc > 1) ? argv[1] : ".";
    const char *out = (argc > 2) ? argv[2] : "shots";
    DWORD pid;
    HWND h;
    int i;

    CreateDirectoryA(out, NULL);
    pid = launch(dir);
    if (!pid) return 1;
    printf("launched pid=%lu dir=%s out=%s\n", pid, dir, out);

    for (i = 0; i < 200; i++) {
        Sleep(100);
        h = find_game_window(pid);
        if (h) break;
    }
    if (!h) { printf("window not found\n"); return 1; }
    printf("hwnd=%p\n", (void *)h);

    SetForegroundWindow(h);
    SetFocus(h);
    Sleep(6000);
    shot(h, out, "f00_title.bmp");
    if (!alive(h, pid)) return 2;

    printf("--- Enter: new game ---\n");
    tap_key(VK_RETURN);
    Sleep(3000);
    shot(h, out, "f01_map.bmp");
    if (!alive(h, pid)) return 2;

    printf("--- Down x3 ---\n");
    tap_n(VK_DOWN, 3, 500, h);
    Sleep(1500);
    shot(h, out, "f02_down.bmp");
    if (!alive(h, pid)) return 2;

    printf("--- Left x3 ---\n");
    tap_n(VK_LEFT, 3, 500, h);
    Sleep(1500);
    shot(h, out, "f03_left.bmp");
    if (!alive(h, pid)) return 2;

    printf("--- Esc: menu ---\n");
    tap_key(VK_ESCAPE);
    Sleep(2500);
    shot(h, out, "f04_menu.bmp");
    if (!alive(h, pid)) return 2;

    printf("--- Esc: close menu ---\n");
    tap_key(VK_ESCAPE);
    Sleep(1500);
    shot(h, out, "f05_closed.bmp");
    if (!alive(h, pid)) return 2;

    printf("DONE\n");
    return 0;
}
