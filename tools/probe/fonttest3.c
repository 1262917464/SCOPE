#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <stdio.h>
static void render(const char *u8name, int hparam, const char *out)
{
    wchar_t w[256], text[64];
    MultiByteToWideChar(CP_UTF8, 0, u8name, -1, w, 256);
    MultiByteToWideChar(CP_UTF8, 0, "新的劇情", -1, text, 64);
    HFONT hf = CreateFontW(hparam, 0,0,0, FW_NORMAL, FALSE,FALSE,FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           ANTIALIASED_QUALITY, DEFAULT_PITCH, w);
    HDC sdc = GetDC(NULL);
    HDC dc = CreateCompatibleDC(sdc);
    HBITMAP bm = CreateCompatibleBitmap(sdc, 74, 24);
    HGDIOBJ ob = SelectObject(dc, bm), of = SelectObject(dc, hf);
    RECT rc = {0,0,74,24};
    FillRect(dc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(255,255,255));
    TextOutW(dc, 0, 0, text, 4);
    GdiFlush();
    /* 写 BMP */
    BITMAPINFO bi; memset(&bi,0,sizeof bi);
    bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); bi.bmiHeader.biWidth=74;
    bi.bmiHeader.biHeight=-24; bi.bmiHeader.biPlanes=1; bi.bmiHeader.biBitCount=32;
    GetDIBits(dc, bm, 0, 24, NULL, &bi, DIB_RGB_COLORS);
    unsigned char *buf = malloc(74*24*4);
    GetDIBits(dc, bm, 0, 24, buf, &bi, DIB_RGB_COLORS);
    FILE *f = fopen(out, "wb");
    BITMAPFILEHEADER fh; fh.bfType=0x4D42; fh.bfOffBits=54; fh.bfSize=54+74*24*4;
    fh.bfReserved1=fh.bfReserved2=0;
    fwrite(&fh,sizeof fh,1,f); fwrite(&bi.bmiHeader,sizeof bi.bmiHeader,1,f);
    for (int y=0;y<24;y++) fwrite(buf+y*74*4,1,74*4,f);
    fclose(f); free(buf);
    SelectObject(dc,ob); SelectObject(dc,of); DeleteObject(bm); DeleteObject(hf); DeleteDC(dc); ReleaseDC(NULL,sdc);
}
int main(void)
{
    render("宋体", -22, "ft_simsun-22.bmp");
    render("宋体",  22, "ft_simsun+22.bmp");
    render("宋体", -16, "ft_simsun-16.bmp");
    render("SimSun", -16, "ft_simsun16b.bmp");
    return 0;
}
