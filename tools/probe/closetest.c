/* closetest.c — B43 验证: 窗口关闭 → exit 0 → 干净退出
 * 流程: 启动 → 等窗口 → Enter 进图 → 等 3s → PostMessage(WM_CLOSE) →
 *       等进程退出 (最多 8s)。
 * 预期: 进程数秒内退出 (SystemExit → 主循环清理 → return)。
 *       若有错误弹窗阻塞, 超时 = FAIL。
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

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    STARTUPINFOA si; PROCESS_INFORMATION pi; char cmd[512];
    HWND h; int i;

    memset(&si, 0, sizeof si); si.cb = sizeof si;
    sprintf(cmd, "\"%s\\Game.exe\"", dir);
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, dir, &si, &pi)) {
        printf("launch failed\n"); return 1;
    }
    printf("pid=%lu\n", pi.dwProcessId);
    for (i = 0; i < 200; i++) { Sleep(100); if ((h = find_game_window(pi.dwProcessId))) break; }
    if (!h) { printf("window not found\n"); return 1; }
    SetForegroundWindow(h); SetFocus(h);
    Sleep(6000);

    /* 进图 (有场景在跑, Graphics.update 每帧执行) */
    keybd_event(VK_RETURN, 0, 0, 0); Sleep(90);
    keybd_event(VK_RETURN, 0, KEYEVENTF_KEYUP, 0);
    Sleep(3000);
    if (!IsWindow(h)) { printf("window died before close test\n"); return 2; }
    printf("--- posting WM_CLOSE ---\n");
    PostMessageA(h, WM_CLOSE, 0, 0);

    DWORD wait = WaitForSingleObject(pi.hProcess, 8000);
    if (wait == WAIT_OBJECT_0) {
        DWORD code = 0;
        GetExitCodeProcess(pi.hProcess, &code);
        printf("PASS: process exited, code=%lu\n", code);
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        return 0;
    }
    if (wait == WAIT_TIMEOUT) {
        /* 检查是否有弹窗 (对话框类 #32770) */
        HWND dlg = NULL;
        for (i = 0; i < 10; i++) {
            dlg = FindWindowExA(NULL, NULL, "#32770", NULL);
            if (dlg) break;
            Sleep(200);
        }
        printf("FAIL: process still alive after 8s%s\n", dlg ? " (dialog present!)" : "");
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        return 2;
    }
    printf("wait failed err=%lu\n", GetLastError());
    return 3;
}
