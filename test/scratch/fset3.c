#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static wchar_t **g_set;
static int g_n, g_cap;

static int CALLBACK cb(const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD t, LPARAM lp)
{
    (void)tm; (void)t; (void)lp;
    if (lf && lf->lfFaceName[0]) {
        if (g_n == g_cap) {
            g_cap = g_cap ? g_cap * 2 : 512;
            g_set = (wchar_t **)realloc(g_set, sizeof(wchar_t *) * g_cap);
        }
        g_set[g_n] = (wchar_t *)malloc(sizeof(wchar_t) * LF_FACESIZE);
        lstrcpynW(g_set[g_n], lf->lfFaceName, LF_FACESIZE);
        g_n++;
    }
    return 1;
}

static void put_utf8(const wchar_t *s)
{
    char buf[1024];
    int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, buf, sizeof buf - 1, NULL, NULL);
    if (n > 0) { buf[n - 1] = 0; fputs(buf, stdout); }
}

int main(void)
{
    LOGFONTW lf;
    HDC dc;
    int i, j;

    SetConsoleOutputCP(CP_UTF8);

    memset(&lf, 0, sizeof lf);
    lf.lfCharSet = DEFAULT_CHARSET;
    dc = GetDC(NULL);
    EnumFontFamiliesExW(dc, &lf, (FONTENUMPROCW)cb, 0, 0);
    ReleaseDC(NULL, dc);
    printf("total enumerated families: %d (cap %d, truncated=%s)\n",
           g_n, g_cap, g_n == g_cap ? "YES" : "no");

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
    printf("--- Font#name candidates (Ruby array order) ---\n");
    for (i = 0; i < 9; i++) {
        int hit = 0, at = -1;
        for (j = 0; j < g_n; j++)
            if (lstrcmpiW(g_set[j], want[i]) == 0) { hit = 1; at = j; break; }
        printf("  [%d] ", i); put_utf8(want[i]);
        printf("  -> %s", hit ? "IN SET" : "MISSING");
        if (hit) printf(" (enum idx %d)", at);
        printf("\n");
    }

    printf("--- families matching LXGW / WenKai / Kai ---\n");
    for (j = 0; j < g_n; j++) {
        const wchar_t *s = g_set[j];
        if (wcsstr(s, L"LXGW") || wcsstr(s, L"WenKai") || wcsstr(s, L"wenkai")
            || wcsstr(s, L"Kai") || wcsstr(s, L"\u6977")) {
            printf("  idx %4d: ", j); put_utf8(s); printf("\n");
        }
    }
    return 0;
}
