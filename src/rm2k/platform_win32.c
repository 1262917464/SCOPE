/*
 * platform_win32.c —— 平台层：窗口、定时、消息泵
 *
 * 对应原版：
 *   - TFormLcfGameMain 的窗口类（VCL TForm，但 DFM 里只有 287x267 占位尺寸）
 *   - 客户区绘制在 320x240（构造函数 edx=0x140 ecx=0xF0）
 *   - 定时用 timeSetEvent / timeGetTime（已确认导入表）
 *
 * M2 目标：开窗 + 消息泵 + 定时，画面先填黑，证明循环骨架成立。
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>

#include "rm2k_delphi.h"

extern void rm2k_aurora_draw_main(AuroraDraw *self);

uint32_t rm2k_platform_tick(void)
{
    return (uint32_t)GetTickCount();
}

void rm2k_platform_wait(uint32_t ms)
{
    Sleep(ms);
}

static volatile int g_running = 1;

static LRESULT CALLBACK rm2k_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CLOSE:
        g_running = 0;
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        HBRUSH br;
        GetClientRect(hwnd, &rc);
        br = CreateSolidBrush(RGB(0, 0, 0));
        FillRect(hdc, &rc, br);
        DeleteObject(br);
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

int main(void)
{
    WNDCLASSA wc;
    HWND hwnd;
    AuroraDraw draw;
    MSG msg;
    RECT rc;
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;

    memset(&draw, 0, sizeof(draw));
    memset(&wc, 0, sizeof(wc));

    wc.lpfnWndProc   = rm2k_wndproc;
    wc.hInstance     = GetModuleHandleA(NULL);
    wc.lpszClassName = "TFormLcfGameMain";
    wc.hCursor       = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    if (!RegisterClassA(&wc)) {
        return 1;
    }

    /* 客户区 320x240，标题栏按窗口样式修正 */
    rc.left = 0; rc.top = 0;
    rc.right = 320; rc.bottom = 240;
    AdjustWindowRect(&rc, style, FALSE);

    hwnd = CreateWindowExA(0, "TFormLcfGameMain", "RPG_RT",
                           style, CW_USEDEFAULT, CW_USEDEFAULT,
                           rc.right - rc.left, rc.bottom - rc.top,
                           NULL, NULL, wc.hInstance, NULL);
    if (!hwnd) {
        return 2;
    }
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    /* 对应 FormCreate: 构造 TAuroraDraw，帧率 20 */
    rm2k_aurora_draw_create(&draw, 20);

    /*
     * 原版在主循环里自己抽消息（AuroraDrawMain 内部调 0x4561D8/0x4562EC）。
     * 这里把 GetMessage/PeekMessage 放在平台层，循环体保持原版结构。
     */
    while (g_running) {
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                g_running = 0;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (!g_running) {
            break;
        }
        rm2k_aurora_draw_main(&draw);
    }

    DestroyWindow(hwnd);
    UnregisterClassA("TFormLcfGameMain", GetModuleHandleA(NULL));
    return 0;
}
