/* f12test.c — F12 Reset 重启机制验证
 * 流程: 启动 → 等标题 → Enter 进图 → 截图(进图证据) →
 *       按住 F12 500ms 松开 → 等 4s → 截图 → 验证窗口仍存活(重启未崩)
 * 预期: F12 后回到标题画面 (脚本重新 eval), 进程不退出。
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
            char cls[64] = {0};
            GetClassNameA(h, cls, sizeof(cls));
            if (!strcmp(cls, "RGSS Player") || strstr(cls, "RGSS")) return h;
        }
    }
    return NULL;
}

static DWORD launch(const char *dir)
{
    STARTUPINFOA si; PROCESS_INFORMATION pi; char cmd[512];
    memset(&si, 0, sizeof si); si.cb = sizeof si;
    sprintf(cmd, "\"%s\\Game.exe\"", dir);
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, dir, &si, &pi)) return 0;
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return pi.dwProcessId;
}

static void shot(HWND h, const char *path)
{
    HDC wdc, mdc; HBITMAP bmp, old; RECT rc; BITMAPINFO bi; void *bits;
    FILE *f; BITMAPFILEHEADER bfh; int w, hh, stride;
    if (!IsWindow(h)) { printf("  (window gone, no shot)\n"); return; }
    GetClientRect(h, &rc); w = rc.right; hh = rc.bottom;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -hh;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    wdc = GetDC(h); mdc = CreateCompatibleDC(wdc);
    bmp = CreateDIBSection(mdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    old = SelectObject(mdc, bmp);
    BitBlt(mdc, 0, 0, w, hh, wdc, 0, 0, SRCCOPY); GdiFlush();
    stride = w * 4;
    f = fopen(path, "wb");
    if (f) {
        bfh.bfType = 0x4D42; bfh.bfOffBits = 54; bfh.bfSize = 54 + stride * hh;
        fwrite(&bfh, sizeof bfh, 1, f);
        fwrite(&bi.bmiHeader, sizeof(BITMAPINFOHEADER), 1, f);
        for (unsigned char *row = bits; row < (unsigned char*)bits + stride*hh; row += stride)
            fwrite(row, 1, stride, f);
        fclose(f);
        printf("  saved %s\n", path);
    }
    SelectObject(mdc, old); DeleteObject(bmp); DeleteDC(mdc); ReleaseDC(h, wdc);
}

static void tap_key(WORD vk, int hold_ms)
{
    keybd_event((BYTE)vk, (BYTE)MapVirtualKeyA(vk, 0), 0, 0);
    Sleep(hold_ms);
    keybd_event((BYTE)vk, (BYTE)MapVirtualKeyA(vk, 0), KEYEVENTF_KEYUP, 0);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    const char *out = argc > 2 ? argv[2] : "f12shots";
    DWORD pid; HWND h; int i;

    CreateDirectoryA(out, NULL);
    pid = launch(dir);
    if (!pid) { printf("launch failed\n"); return 1; }
    printf("pid=%lu\n", pid);
    for (i = 0; i < 200; i++) { Sleep(100); if ((h = find_game_window(pid))) break; }
    if (!h) { printf("window not found\n"); return 1; }
    SetForegroundWindow(h); SetFocus(h);
    Sleep(6000);

    char p[MAX_PATH];
    sprintf(p, "%s\\f00_title.bmp", out); shot(h, p);

    printf("--- Enter: new game ---\n");
    tap_key(VK_RETURN, 90); Sleep(3000);
    sprintf(p, "%s\\f01_map.bmp", out); shot(h, p);

    printf("--- hold F12 500ms (expect Reset restart) ---\n");
    tap_key(VK_F12, 500);
    Sleep(4000);
    if (!IsWindow(h)) {
        printf("FAIL: window destroyed after F12 (no Reset handling)\n");
        return 2;
    }
    sprintf(p, "%s\\f02_after_f12.bmp", out); shot(h, p);
    printf("PASS: window alive after F12\n");

    /* 再按 Enter: 若真的回到标题, 应再次进图 */
    printf("--- Enter again: should re-enter map if restarted ---\n");
    tap_key(VK_RETURN, 90); Sleep(3000);
    sprintf(p, "%s\\f03_after_enter2.bmp", out); shot(h, p);
    printf("DONE alive=%d\n", IsWindow(h) ? 1 : 0);
    return 0;
}
