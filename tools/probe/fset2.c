#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

static wchar_t g_set[4000][LF_FACESIZE];
static int g_n;

static int CALLBACK cb(const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD t, LPARAM lp)
{
    (void)tm; (void)t; (void)lp;
    if (lf && lf->lfFaceName[0] && g_n < 4000) {
        lstrcpynW(g_set[g_n], lf->lfFaceName, LF_FACESIZE);
        g_n++;
    }
    return 1;
}

static void scan(const char *tag)
{
    LOGFONTW lf;
    HDC dc;
    g_n = 0;
    memset(&lf, 0, sizeof lf);
    lf.lfCharSet = DEFAULT_CHARSET;
    dc = GetDC(NULL);
    EnumFontFamiliesExW(dc, &lf, (FONTENUMPROCW)cb, 0, 0);
    ReleaseDC(NULL, dc);
    printf("[%s] total families: %d\n", tag, g_n);

    const wchar_t *want[] = {
        L"\u971E\u9E5C\u6587\u6977\u7B49\u5BBD TC Bold",
        L"\u971E\u9E5C\u6587\u6977 \u5C4F\u5E55\u9605\u8BFB\u7248 R",
        L"LXGW WenKai Mono TC",
        L"DFKai-SB",
        L"\u6A19\u6977\u9AD4",
        L"PMingLiU",
        L"\u65B0\u7D30\u660E\u9AD4",
        L"Verdana",
        L"Arial Unicode MS",
    };
    for (int i = 0; i < 9; i++) {
        int hit = 0;
        for (int j = 0; j < g_n; j++)
            if (lstrcmpiW(g_set[j], want[i]) == 0) { hit = 1; break; }
        printf("  [%d] %-34ls -> %s\n", i, want[i], hit ? "IN SET" : "MISSING");
    }
}

int main(void)
{
    scan("before AddFontResourceEx");
    int n = AddFontResourceExW(
        L"E:\\rgss2-rebuild\\test\\Project3_orig\\Fonts\\kaiu.ttf", FR_PRIVATE, 0);
    printf("AddFontResourceExW(kaiu.ttf) -> %d\n", n);
    scan("after AddFontResourceEx");

    /* 直接试建 DFKai-SB，看能否命中 kaiu.ttf */
    LOGFONTW lf; memset(&lf, 0, sizeof lf);
    lf.lfHeight = 22; lf.lfCharSet = DEFAULT_CHARSET;
    lstrcpynW(lf.lfFaceName, L"DFKai-SB", LF_FACESIZE);
    HFONT hf = CreateFontIndirectW(&lf);
    HDC dc = CreateCompatibleDC(NULL);
    HGDIOBJ old = SelectObject(dc, hf);
    wchar_t got[LF_FACESIZE] = {0};
    GetTextFaceW(dc, LF_FACESIZE, got);
    wprintf(L"CreateFont(DFKai-SB,22) -> actual face '%ls'\n", got);
    SelectObject(dc, old); DeleteObject(hf); DeleteDC(dc);
    return 0;
}
