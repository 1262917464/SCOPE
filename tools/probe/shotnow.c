/* shotnow.c — 对任意 "RGSS Player" 窗口截一帧: shotnow.exe <out.bmp> */
#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    HWND h = NULL, f = NULL; int w, hh, stride;
    HDC wdc, mdc; HBITMAP bmp, old; RECT rc; BITMAPINFO bi; void *bits; FILE *fp;
    BITMAPFILEHEADER bfh; unsigned char *row;
    const char *out = (argc > 1) ? argv[1] : "shot.bmp";
    while ((f = FindWindowEx(NULL, f, NULL, NULL)) != NULL) {
        char cls[64] = {0};
        GetClassNameA(f, cls, sizeof(cls));
        if (!strcmp(cls, "RGSS Player") && IsWindowVisible(f)) { h = f; break; }
    }
    if (!h) { printf("no window\n"); return 1; }
    GetClientRect(h, &rc); w = rc.right; hh = rc.bottom;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -hh;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    wdc = GetDC(h); mdc = CreateCompatibleDC(wdc);
    bmp = CreateDIBSection(mdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    old = SelectObject(mdc, bmp);
    BitBlt(mdc, 0, 0, w, hh, wdc, 0, 0, SRCCOPY); GdiFlush();
    stride = w * 4;
    fp = fopen(out, "wb");
    if (fp) {
        bfh.bfType = 0x4D42;
        bfh.bfOffBits = sizeof(bfh) + sizeof(bi.bmiHeader);
        bfh.bfSize = bfh.bfOffBits + stride * hh;
        fwrite(&bfh, sizeof(bfh), 1, fp);
        fwrite(&bi.bmiHeader, sizeof(bi.bmiHeader), 1, fp);
        for (row = bits; row < (unsigned char *)bits + stride * hh; row += stride)
            fwrite(row, 1, stride, fp);
        fclose(fp);
        printf("saved %s %dx%d\n", out, w, hh);
    }
    SelectObject(mdc, old); DeleteObject(bmp); DeleteDC(mdc); ReleaseDC(h, wdc);
    return 0;
}
