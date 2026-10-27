#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

static wchar_t g_set[2000][LF_FACESIZE];
static int g_n;

static int CALLBACK cb(const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD t, LPARAM lp)
{
    (void)tm; (void)t; (void)lp;
    if (lf && lf->lfFaceName[0] && g_n < 2000) {
        lstrcpynW(g_set[g_n], lf->lfFaceName, LF_FACESIZE);
        g_n++;
    }
    return 1;
}

int main(void)
{
    LOGFONTW lf;
    HDC dc;
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

    memset(&lf, 0, sizeof lf);
    lf.lfCharSet = DEFAULT_CHARSET;
    dc = GetDC(NULL);
    EnumFontFamiliesExW(dc, &lf, (FONTENUMPROCW)cb, 0, 0);
    ReleaseDC(NULL, dc);
    printf("total enumerated families: %d\n", g_n);

    printf("--- Font#name candidates ---\n");
    for (int i = 0; i < 9; i++) {
        int hit = 0;
        for (int j = 0; j < g_n; j++)
            if (lstrcmpiW(g_set[j], want[i]) == 0) { hit = 1; break; }
        printf("  [%d] %-40ls -> %s\n", i, want[i], hit ? "IN SET" : "MISSING");
    }

    printf("--- families whose name contains Kai/kai/wenkai/LXGW/DFKai ---\n");
    for (int j = 0; j < g_n; j++) {
        const wchar_t *s = g_set[j];
        if (wcsstr(s, L"Kai") || wcsstr(s, L"kai") || wcsstr(s, L"WenKai")
            || wcsstr(s, L"LXGW") || wcsstr(s, L"DFKai") || wcsstr(s, L"\u6977"))
            printf("  %ls\n", s);
    }
    return 0;
}
