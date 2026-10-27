/* keymtest.c — B10 键位验证
 * 标题画面: 物理 'C' 应无效 (废弃键) → 仍停标题;
 *           物理 'Z' 应触发确认 (C) → 进入地图。
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

static int map_on_screen(HWND h)   /* 地图 = 屏幕下半大量海蓝 (与标题天空白/蓝渐变区分) */
{
    HDC wdc = GetDC(h);
    COLORREF c1 = GetPixel(wdc, 60, 380), c2 = GetPixel(wdc, 480, 380);
    ReleaseDC(h, wdc);
    /* 标题画面底部是白色房屋 (高亮度), 地图底部是海面 (B 明显大于 R) */
    return (GetBValue(c1) - GetRValue(c1) > 40) || (GetBValue(c2) - GetRValue(c2) > 40);
}

static void tap(WORD vk)
{
    keybd_event((BYTE)vk, (BYTE)MapVirtualKeyA(vk, 0), 0, 0);
    Sleep(90);
    keybd_event((BYTE)vk, (BYTE)MapVirtualKeyA(vk, 0), KEYEVENTF_KEYUP, 0);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    STARTUPINFOA si; PROCESS_INFORMATION pi; char cmd[512];
    HWND h; int i;

    memset(&si, 0, sizeof si); si.cb = sizeof si;
    sprintf(cmd, "\"%s\\Game.exe\"", dir);
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, dir, &si, &pi)) return 1;
    for (i = 0; i < 200; i++) { Sleep(100); if ((h = find_game_window(pi.dwProcessId))) break; }
    if (!h) { printf("window not found\n"); return 1; }
    SetForegroundWindow(h); SetFocus(h);
    Sleep(6000);
    printf("title: map_on_screen=%d (expect 0)\n", map_on_screen(h));

    printf("--- tap physical 'C' (expect: nothing, deprecated) ---\n");
    tap('C'); Sleep(2500);
    int after_c = map_on_screen(h);
    printf("  map_on_screen=%d (expect 0)\n", after_c);

    printf("--- tap physical 'Z' (expect: C → new game → map) ---\n");
    tap('Z'); Sleep(3000);
    int after_z = IsWindow(h) ? map_on_screen(h) : -1;
    printf("  map_on_screen=%d (expect 1)\n", after_z);

    int pass = (!after_c) && (after_z == 1);
    printf("%s\n", pass ? "PASS: C deprecated, Z=C" : "FAIL");
    if (IsWindow(h)) PostMessageA(h, WM_CLOSE, 0, 0);
    return pass ? 0 : 2;
}
