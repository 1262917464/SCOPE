/* cursortest.c — 验证标题画面光标能否移动 + 闪烁
 * 启动游戏 → 截图 t0 → 按 DOWN → 截图 t1 → 按 UP → 截图 t2
 * 对比光标框位置变化。
 */
#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <stdio.h>
#include <string.h>

static DWORD launch(const char *dir)
{
    STARTUPINFOA si; PROCESS_INFORMATION pi; char cmd[512];
    memset(&si,0,sizeof si); si.cb = sizeof si;
    sprintf(cmd, "\"%s\\Game.exe\"", dir);
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, dir, &si, &pi)) return 0;
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return pi.dwProcessId;
}

static HWND find_game_window(DWORD pid)
{
    HWND h = NULL;
    while ((h = FindWindowEx(NULL, h, NULL, NULL)) != NULL) {
        DWORD wpid = 0; GetWindowThreadProcessId(h, &wpid);
        if (wpid == pid && IsWindowVisible(h)) {
            char cls[64] = {0};
            GetClassNameA(h, cls, sizeof cls);
            if (!strcmp(cls, "RGSS Player")) return h;
        }
    }
    return NULL;
}

static void tap_key(WORD vk)
{
    keybd_event((BYTE)vk, (BYTE)MapVirtualKeyA(vk, 0), 0, 0);
    Sleep(90);
    keybd_event((BYTE)vk, (BYTE)MapVirtualKeyA(vk, 0), KEYEVENTF_KEYUP, 0);
}

/* 在截图中找光标框的 y 位置: 扫描窗口区域, 找白色横线的行 */
static int find_cursor_row(HWND h, const char *path)
{
    /* 读截图文件, 在窗口区域 (186..358, 288..392) 内找白色像素最集中的行 */
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    BITMAPFILEHEADER bfh; BITMAPINFOHEADER bih;
    fread(&bfh, sizeof bfh, 1, f);
    fread(&bih, sizeof bih, 1, f);
    int W = bih.biWidth, H = -bih.biHeight;
    fseek(f, bfh.bfOffBits, SEEK_SET);
    unsigned char *buf = malloc(W * H * 4);
    fread(buf, 1, W * H * 4, f);
    fclose(f);

    int best_row = -1; long best = 0;
    for (int y = 300; y < 390; y++) {
        long white = 0;
        for (int x = 195; x < 350; x++) {
            const unsigned char *p = buf + ((size_t)y * W + x) * 4;
            if (p[0] > 240 && p[1] > 240 && p[2] > 240) white++;
        }
        if (white > best) { best = white; best_row = y; }
    }
    free(buf);
    return best_row;
}

int main(int argc, char **argv)
{
    const char *dir = argv[1];
    DWORD pid = launch(dir);
    if (!pid) return 1;
    HWND h = NULL;
    for (int i = 0; i < 200 && !h; i++) { Sleep(100); h = find_game_window(pid); }
    if (!h) { printf("no window\n"); return 1; }
    SetForegroundWindow(h); Sleep(300); Sleep(7000);

    /* --- 截 3 张, 间隔 300ms, 看闪烁 (光标区亮度) --- */
    printf("== blink sample (cursor border row brightness) ==\n");
    for (int i = 0; i < 20; i++) {
        /* 用 blinkprobe 同款区域均值 */
        HDC wdc = GetDC(h);
        BITMAPINFO bi; void *bits = NULL;
        memset(&bi,0,sizeof bi);
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = 140; bi.bmiHeader.biHeight = -24;
        bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        HBITMAP bmp = CreateDIBSection(wdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
        HDC mdc = CreateCompatibleDC(wdc);
        HGDIOBJ old = SelectObject(mdc, bmp);
        BitBlt(mdc, 0, 0, 140, 24, wdc, 202, 304, SRCCOPY);
        GdiFlush();
        long sum = 0;
        const unsigned char *p = (const unsigned char *)bits;
        for (int k = 0; k < 140*24; k++) sum += (p[k*4]+p[k*4+1]+p[k*4+2])/3;
        printf("  t=%4dms mean=%.2f\n", i*100, (double)sum/(140*24));
        SelectObject(mdc, old); DeleteObject(bmp); DeleteDC(mdc); ReleaseDC(h, wdc);
        Sleep(100);
    }

    /* --- 按 DOWN / UP, 看光标行是否移动 --- */
    system("del /q ct_*.bmp 2>nul");
    printf("== cursor movement ==\n");
    /* 先等到 blink 峰值附近, 截一张 */
    Sleep(200);
    HDC wdc = GetDC(h);
    (void)wdc; ReleaseDC(h, wdc);

    /* 截图 helper: 直接写 BMP */
    /* t0 */
    {
        HDC dc = GetDC(h);
        BITMAPINFO bi; void *bits = NULL;
        memset(&bi,0,sizeof bi);
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = 544; bi.bmiHeader.biHeight = -416;
        bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
        HDC m = CreateCompatibleDC(dc); HGDIOBJ o = SelectObject(m, bmp);
        BitBlt(m,0,0,544,416,dc,0,0,SRCCOPY); GdiFlush();
        FILE *f = fopen("ct_t0.bmp","wb");
        BITMAPFILEHEADER fh; fh.bfType=0x4D42; fh.bfOffBits=54; fh.bfSize=54+544*416*4;
        fh.bfReserved1=fh.bfReserved2=0;
        fwrite(&fh,sizeof fh,1,f); fwrite(&bi.bmiHeader,sizeof bi.bmiHeader,1,f);
        fwrite(bits,1,544*416*4,f); fclose(f);
        SelectObject(m,o); DeleteObject(bmp); DeleteDC(m); ReleaseDC(h,dc);
    }
    tap_key(VK_DOWN); Sleep(500);
    /* t1 */
    {
        HDC dc = GetDC(h);
        BITMAPINFO bi; void *bits = NULL;
        memset(&bi,0,sizeof bi);
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = 544; bi.bmiHeader.biHeight = -416;
        bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
        HDC m = CreateCompatibleDC(dc); HGDIOBJ o = SelectObject(m, bmp);
        BitBlt(m,0,0,544,416,dc,0,0,SRCCOPY); GdiFlush();
        FILE *f = fopen("ct_t1.bmp","wb");
        BITMAPFILEHEADER fh; fh.bfType=0x4D42; fh.bfOffBits=54; fh.bfSize=54+544*416*4;
        fh.bfReserved1=fh.bfReserved2=0;
        fwrite(&fh,sizeof fh,1,f); fwrite(&bi.bmiHeader,sizeof bi.bmiHeader,1,f);
        fwrite(bits,1,544*416*4,f); fclose(f);
        SelectObject(m,o); DeleteObject(bmp); DeleteDC(m); ReleaseDC(h,dc);
    }
    printf("  t0 cursor row = %d\n", find_cursor_row(h, "ct_t0.bmp"));
    printf("  t1 cursor row = %d\n", find_cursor_row(h, "ct_t1.bmp"));
    tap_key(VK_UP);
    Sleep(300);
    PostMessageA(h, WM_CLOSE, 0, 0);
    return 0;
}
