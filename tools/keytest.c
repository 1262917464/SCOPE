/* keytest.c — 自动化按键测试
 * 启动 Game.exe, 等标题画面出现, 注入 Input.update + 按键读取探针,
 * 用 PostMessage / keybd_event 模拟按键, 记录 Input 状态变化并截图。
 *
 * 用法: keytest.exe <项目目录>
 */
#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <stdio.h>
#include <string.h>

static HWND g_game;

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

/* 拉起进程，返回 pid */
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

static void shot(HWND h, const char *path)
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

    GetClientRect(h, &rc);
    w = rc.right; hh = rc.bottom;
    if (w <= 0 || hh <= 0) { printf("bad client rect\n"); return; }

    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -hh;   /* top-down */
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
        printf("saved %s (%dx%d)\n", path, w, hh);
    }

    SelectObject(mdc, old);
    DeleteObject(bmp);
    DeleteDC(mdc);
    ReleaseDC(h, wdc);
}

/* 统计截图中非黑像素比例，用来判断是否黑屏 */
static double nonblack_ratio(const char *path)
{
    FILE *f = fopen(path, "rb");
    BITMAPFILEHEADER bfh;
    BITMAPINFOHEADER bih;
    unsigned char *buf;
    long total, nz = 0, i;

    if (!f) return -1.0;
    fread(&bfh, sizeof(bfh), 1, f);
    fread(&bih, sizeof(bih), 1, f);
    total = bih.biWidth * (long)(bih.biHeight < 0 ? -bih.biHeight : bih.biHeight);
    fseek(f, bfh.bfOffBits, SEEK_SET);
    buf = malloc(bih.biSizeImage ? bih.biSizeImage : total * 4);
    fread(buf, 1, total * 4, f);
    for (i = 0; i < total; i++) {
        unsigned char *p = buf + i * 4;
        if (p[0] || p[1] || p[2]) nz++;
    }
    free(buf);
    fclose(f);
    return total ? (double)nz / (double)total : 0.0;
}

/* 注入真实按键: keybd_event 会更新 GetAsyncKeyState 的全局状态,
 * PostMessage 不会 —— 所以这里必须用 keybd_event。 */
static void tap_key(WORD vk)
{
    keybd_event((BYTE)vk, (BYTE)MapVirtualKeyA(vk, 0), 0, 0);
    Sleep(90);
    keybd_event((BYTE)vk, (BYTE)MapVirtualKeyA(vk, 0), KEYEVENTF_KEYUP, 0);
}

int main(int argc, char **argv)
{
    const char *dir = (argc > 1) ? argv[1] : ".";
    DWORD pid;
    HWND h;
    int i;

    pid = launch(dir);
    if (!pid) return 1;
    printf("launched pid=%lu\n", pid);

    /* 等窗口 */
    for (i = 0; i < 200; i++) {
        Sleep(100);
        h = find_game_window(pid);
        if (h) break;
    }
    if (!h) { printf("window not found\n"); return 1; }
    g_game = h;
    printf("hwnd=%p\n", (void *)h);

    /* 等标题画面稳定 (脚本 eval + 首帧渲染) */
    Sleep(6000);
    shot(h, "kt_00_title.bmp");
    printf("title nonblack=%.4f\n", nonblack_ratio("kt_00_title.bmp"));

    /* 发 C 键 (Enter) — 标题画面应该切到新游戏 */
    printf("--- sending Enter (Input::C) ---\n");
    SetForegroundWindow(h);
    SetFocus(h);
    Sleep(400);
    for (i = 0; i < 3; i++) {
        tap_key(VK_RETURN);
        Sleep(500);
        printf("  enter press %d\n", i);
        if (!IsWindow(h)) { printf("  window GONE after enter %d\n", i); break; }
    }
    Sleep(2500);
    if (!IsWindow(h)) {
        printf("WINDOW DESTROYED. process alive? ");
        /* 打开进程句柄看退出码 */
        HANDLE ph = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
        if (ph) {
            DWORD code = 0;
            GetExitCodeProcess(ph, &code);
            printf("exitcode=%lu%s\n", code, code == STILL_ACTIVE ? " (ALIVE)" : "");
            CloseHandle(ph);
        } else printf("cannot open, err=%lu\n", GetLastError());
        return 2;
    }
    shot(h, "kt_01_after_enter.bmp");
    printf("after_enter nonblack=%.4f\n", nonblack_ratio("kt_01_after_enter.bmp"));

    /* 发方向键 */
    printf("--- sending Down (Input::DOWN) ---\n");
    for (i = 0; i < 3; i++) {
        if (!IsWindow(h)) { printf("  window GONE during down\n"); break; }
        tap_key(VK_DOWN);
        Sleep(400);
    }
    Sleep(1200);
    if (IsWindow(h)) {
        shot(h, "kt_02_after_down.bmp");
        printf("after_down nonblack=%.4f\n", nonblack_ratio("kt_02_after_down.bmp"));
    }

    /* 发 C 键确认 */
    printf("--- sending Enter again ---\n");
    if (IsWindow(h)) {
        tap_key(VK_RETURN);
        Sleep(3000);
        if (IsWindow(h)) {
            shot(h, "kt_03_final.bmp");
            printf("final nonblack=%.4f\n", nonblack_ratio("kt_03_final.bmp"));
        }
    }
    if (!IsWindow(h)) {
        HANDLE ph = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
        DWORD code = 0;
        if (ph) { GetExitCodeProcess(ph, &code); CloseHandle(ph); }
        printf("window gone; exitcode=%lu\n", code);
    }
    return 0;
}
