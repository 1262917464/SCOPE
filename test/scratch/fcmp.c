/* fcmp.c — 用候选字体渲染同一串字, 与原版截图掩码比对
 * 用法: fcmp.exe <orig_png_region_as_txt>
 * 直接读原版截图的字芯掩码 (从 f04_menu.png 的 "特殊技能" 行),
 * 对每个候选字体用 GGO 渲染并求 IoU。 */
#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 候选名 (UTF-8 -> L"") */
static const wchar_t *CANDS[] = {
    L"\u971e\u9e5c\u6587\u6977\u7b49\u5bbd TC Bold",
    L"\u971e\u9e5c\u6587\u6977 \u5c4f\u5e55\u9605\u8bfb\u7248 R",
    L"LXGW WenKai Mono TC",
    L"DFKai-SB",
    L"\u6a19\u6977\u9ad4",
    L"PMingLiU",
    L"\u65b0\u7d30\u660e\u9ad4",
    L"Verdana",
    L"Arial Unicode MS",
    L"MS Gothic",
    L"SimHei",
    L"\u5fae\u8f6f\u96c5\u9ed1",
    L"\u9ed1\u4f53",
    L"\u5fae\u8f6f\u96c5\u9ed1 UI",
    L"Microsoft JhengHei",
    L"\u5fae\u8f6f\u6b63\u9ed1\u4f53",
    L"SimSun",
    L"\u5b8b\u4f53",
    NULL
};

static int CALLBACK cb(const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD t, LPARAM lp)
{
    (void)tm; (void)t; (void)lp;
    return 1;
}

int wmain(void)
{
    SetConsoleOutputCP(CP_UTF8);
    AddFontResourceExW(L"E:\\rgss2-rebuild\\test\\Project3_orig\\Fonts\\kaiu.ttf",
                       FR_PRIVATE, 0);

    /* 原版字芯: 从 f04_menu.png 由外部脚本提供的坐标, 这里只打印度量,
     * 实际 IoU 比对在 Python 侧做 (需要 PNG 解码) */
    HDC dc = CreateCompatibleDC(NULL);
    printf("cand\ttmH\ttmAve\tcellX(特)\tbbW(特)\tbbH(特)\n");
    for (int i = 0; CANDS[i]; i++) {
        LOGFONTW lf;
        memset(&lf, 0, sizeof lf);
        lf.lfHeight = 22;
        lf.lfCharSet = DEFAULT_CHARSET;
        lf.lfQuality = DEFAULT_QUALITY;
        lstrcpynW(lf.lfFaceName, CANDS[i], LF_FACESIZE);
        HFONT hf = CreateFontIndirectW(&lf);
        if (!hf) { printf("(create failed)\n"); continue; }
        HGDIOBJ old = SelectObject(dc, hf);
        wchar_t got[LF_FACESIZE] = {0};
        GetTextFaceW(dc, LF_FACESIZE, got);
        TEXTMETRICW tm;
        GetTextMetricsW(dc, &tm);
        MAT2 mat = { {0,1},{0,0},{0,0},{0,1} };
        GLYPHMETRICS gm;
        DWORD sz = GetGlyphOutlineW(dc, L'\u7279', 6, &gm, 0, NULL, &mat);
        char u8[512];
        WideCharToMultiByte(CP_UTF8, 0, got, -1, u8, sizeof u8, NULL, NULL);
        printf("%-26s\t%d\t%d\t%d\t%u\t%u\t(sz=%lu)\n",
               u8, tm.tmHeight, tm.tmAveCharWidth,
               (int)gm.gmCellIncX, gm.gmBlackBoxX, gm.gmBlackBoxY,
               (unsigned long)sz);
        SelectObject(dc, old);
        DeleteObject(hf);
    }
    DeleteDC(dc);
    return 0;
}
