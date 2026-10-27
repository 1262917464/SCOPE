/*
 * rgss_image.cpp — GDI+ 图片加载（C++ 单元，供 rgss_bitmap.c 调用）
 */
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

using namespace Gdiplus;

extern "C" int rgss_load_image(const char *path, unsigned int **out_bits,
                               int *out_w, int *out_h)
{
    static int s_ready = 0;
    static ULONG_PTR s_token = 0;
    if (!s_ready) {
        GdiplusStartupInput si;
        if (GdiplusStartup(&s_token, &si, NULL) != Ok) return 0;
        s_ready = 1;
    }

    /* 原版行为: 路径无扩展名时自动尝试常见图片扩展名 */
    const char *exts[] = { "", ".png", ".jpg", ".jpeg", ".bmp", ".gif", NULL };
    WCHAR wpath[MAX_PATH * 2];
    Bitmap *bmp = NULL;

    for (int i = 0; exts[i]; i++) {
        char full[MAX_PATH * 2];
        snprintf(full, sizeof full, "%s%s", path, exts[i]);
        MultiByteToWideChar(CP_ACP, 0, full, -1, wpath, MAX_PATH * 2);
        bmp = Bitmap::FromFile(wpath);
        if (bmp && bmp->GetLastStatus() == Ok) break;
        if (bmp) { delete bmp; bmp = NULL; }
    }
    if (!bmp) return 0;
    int w = (int)bmp->GetWidth(), h = (int)bmp->GetHeight();
    if (w <= 0 || h <= 0) { delete bmp; return 0; }

    unsigned int *bits = (unsigned int *)calloc((size_t)w * h, 4);
    if (!bits) { delete bmp; return 0; }

    Rect rc(0, 0, w, h);
    BitmapData bd;
    if (bmp->LockBits(&rc, ImageLockModeRead, PixelFormat32bppARGB, &bd) != Ok) {
        free(bits); delete bmp; return 0;
    }
    for (int y = 0; y < h; y++) {
        const unsigned int *src =
            (const unsigned int *)((const BYTE *)bd.Scan0 + (size_t)y * bd.Stride);
        memcpy(bits + (size_t)y * w, src, (size_t)w * 4);
    }
    bmp->UnlockBits(&bd);
    delete bmp;

    *out_bits = bits; *out_w = w; *out_h = h;
    return 1;
}
