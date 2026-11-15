/* pwshot.c — PrintWindow 截图（不要求前台）: pwshot.exe <out.bmp> [pid] */
#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv)
{
    HWND f = NULL, h = NULL; RECT rc; int w, hh, stride, want = argc>2?atoi(argv[2]):0;
    HDC wdc, mdc; HBITMAP bmp, old; BITMAPINFO bi; void *bits; FILE *fp;
    BITMAPFILEHEADER bfh; unsigned char *row;
    const char *out = argc>1?argv[1]:"pw.bmp";
    while ((f = FindWindowEx(NULL, f, NULL, NULL)) != NULL) {
        char cls[64] = {0}; DWORD pid = 0;
        GetClassNameA(f, cls, sizeof(cls));
        GetWindowThreadProcessId(f, &pid);
        if (!strcmp(cls, "RGSS Player") && (!want || (int)pid == want) && IsWindowVisible(f)) { h = f; break; }
    }
    if (!h) { printf("no window\n"); return 1; }
    GetClientRect(h, &rc); w = rc.right; hh = rc.bottom;
    printf("client %dx%d\n", w, hh);
    if (w <= 0 || hh <= 0) { printf("empty client\n"); return 1; }
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -hh;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    wdc = GetDC(h); mdc = CreateCompatibleDC(wdc);
    bmp = CreateDIBSection(mdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    old = SelectObject(mdc, bmp);
    if (!PrintWindow(h, mdc, 2 /*PW_RENDERFULLCONTENT*/)) printf("PrintWindow failed\n");
    GdiFlush();
    stride = w * 4;
    fp = fopen(out, "wb");
    if (fp) {
        bfh.bfType = 0x4D42; bfh.bfOffBits = sizeof(bfh) + sizeof(bi.bmiHeader);
        bfh.bfSize = bfh.bfOffBits + stride * hh;
        fwrite(&bfh, sizeof(bfh), 1, fp); fwrite(&bi.bmiHeader, sizeof(bi.bmiHeader), 1, fp);
        for (row = bits; row < (unsigned char*)bits + stride*hh; row += stride) fwrite(row, 1, stride, fp);
        fclose(fp); printf("saved %s\n", out);
    }
    SelectObject(mdc, old); DeleteObject(bmp); DeleteDC(mdc); ReleaseDC(wdc, h);
    return 0;
}
