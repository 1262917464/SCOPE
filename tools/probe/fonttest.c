#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <stdio.h>
static void tryname(const char *u8)
{
    wchar_t w[256];
    MultiByteToWideChar(CP_UTF8, 0, u8, -1, w, 256);
    HFONT hf = CreateFontW(-22, 0,0,0, FW_NORMAL, FALSE,FALSE,FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           ANTIALIASED_QUALITY, DEFAULT_PITCH, w);
    HDC dc = CreateCompatibleDC(NULL);
    HGDIOBJ old = SelectObject(dc, hf);
    wchar_t face[256]; GetTextFaceW(dc, 256, face);
    TEXTMETRICW tm; GetTextMetricsW(dc, &tm);
    char face_u8[512];
    WideCharToMultiByte(CP_UTF8,0,face,-1,face_u8,512,NULL,NULL);
    printf("%-30s -> face=%-28s h=%ld avgw=%ld\n", u8, face_u8, tm.tmHeight, tm.tmAveCharWidth);
    SelectObject(dc, old); DeleteObject(hf); DeleteDC(dc);
}
int main(void)
{
    SetConsoleOutputCP(65001);
    tryname("霞鹜文楷等宽 TC Bold");
    tryname("霞鹜文楷等宽 TC");
    tryname("LXGW WenKai Mono TC");
    tryname("霞鹜文楷 屏幕阅读版 R");
    tryname("LXGW WenKai Screen R");
    tryname("DFKai-SB");
    tryname("不存在的中文字体");
    return 0;
}
