#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <stdio.h>
/* 渲染 "新的劇情" 并测 bbox */
static void tryfont(const char *u8name, int hparam)
{
    wchar_t w[256], text[64];
    MultiByteToWideChar(CP_UTF8, 0, u8name, -1, w, 256);
    MultiByteToWideChar(CP_UTF8, 0, "新的劇情", -1, text, 64);
    HFONT hf = CreateFontW(hparam, 0,0,0, FW_NORMAL, FALSE,FALSE,FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           ANTIALIASED_QUALITY, DEFAULT_PITCH, w);
    HDC dc = CreateCompatibleDC(NULL);
    HBITMAP bm = CreateCompatibleBitmap(dc, 200, 60);
    HGDIOBJ ob = SelectObject(dc, bm), of = SelectObject(dc, hf);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(255,255,255));
    RECT rc = {0,0,200,60};
    FillRect(dc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
    TextOutW(dc, 5, 5, text, 4);
    GdiFlush();
    /* 测 bbox */
    int minx=10^9,miny=10^9,maxx=-1,maxy=-1;
    for (int y=0;y<60;y++) for (int x=0;x<200;x++) {
        COLORREF c = GetPixel(dc, x, y);
        if (GetRValue(c)>100) {
            if(x<minx)minx=x; if(x>maxx)maxx=x;
            if(y<miny)miny=y; if(y>maxy)maxy=y;
        }
    }
    wchar_t face[256]; GetTextFaceW(dc, 256, face);
    char fu8[512]; WideCharToMultiByte(CP_UTF8,0,face,-1,fu8,512,NULL,NULL);
    printf("%-24s h=%+d -> face=%-24s text w=%d h=%d\n",
           u8name, hparam, fu8, maxx-minx+1, maxy-miny+1);
    SelectObject(dc, ob); SelectObject(dc, of);
    DeleteObject(bm); DeleteObject(hf); DeleteDC(dc);
}
int main(void)
{
    SetConsoleOutputCP(65001);
    printf("目标 (原版实测): text w=64 h=16\n\n");
    tryfont("霞鹜文楷等宽 TC", -22);
    tryfont("霞鹜文楷等宽 TC",  22);
    tryfont("LXGW WenKai Mono TC", -22);
    tryfont("LXGW WenKai Mono TC",  22);
    tryfont("DFKai-SB", -22);
    tryfont("DFKai-SB",  22);
    tryfont("LXGW WenKai Screen R", -22);
    tryfont("LXGW WenKai Screen R",  22);
    return 0;
}
