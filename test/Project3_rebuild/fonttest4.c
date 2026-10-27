#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
static void render(const char *u8name, int hpx, const char *out)
{
    wchar_t w[256], text[64];
    MultiByteToWideChar(CP_UTF8, 0, u8name, -1, w, 256);
    MultiByteToWideChar(CP_UTF8, 0, "新的劇情", -1, text, 64);
    HFONT hf = CreateFontW(hpx, 0,0,0, FW_NORMAL, FALSE,FALSE,FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           ANTIALIASED_QUALITY, DEFAULT_PITCH, w);
    HDC sdc = GetDC(NULL); HDC dc = CreateCompatibleDC(sdc);
    HBITMAP bm = CreateCompatibleBitmap(sdc, 74, 24);
    HGDIOBJ ob = SelectObject(dc, bm), of = SelectObject(dc, hf);
    RECT rc = {0,0,74,24};
    FillRect(dc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(255,255,255));
    TextOutW(dc, 0, 0, text, 4);
    GdiFlush();
    BITMAPINFO bi; memset(&bi,0,sizeof bi);
    bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); bi.bmiHeader.biWidth=74;
    bi.bmiHeader.biHeight=-24; bi.bmiHeader.biPlanes=1; bi.bmiHeader.biBitCount=32;
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
    render("宋体", -16, "fx_simsun16.bmp");
    render("LXGW WenKai Mono TC", -16, "fx_lxgwmono16.bmp");
    render("DFKai-SB", -16, "fx_dfkai16.bmp");
    render("LXGW WenKai Screen R", -16, "fx_lxgwscreen16.bmp");
    render("標楷體", -16, "fx_biaokai16.bmp");
    return 0;
}
