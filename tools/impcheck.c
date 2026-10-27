/* impcheck.c — 逐一验证脱壳 DLL 导入表可解析性 */
#include <windows.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    /* 直接复用 smoke 的加载失败场景太粗; 这里读 PE 导入表逐项验证 */
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 2;
    static unsigned char d[8 * 1024 * 1024];
    size_t n = fread(d, 1, sizeof d, f);
    fclose(f);
    unsigned pe = *(unsigned *)(d + 0x3c);
    unsigned opt = pe + 24;
    unsigned impRva = *(unsigned *)(d + opt + 96 + 8);
    unsigned char *p = d + impRva;
    int bad = 0;
    for (int i = 0;; i++, p += 20) {
        unsigned oft = *(unsigned *)p, namerva = *(unsigned *)(p + 12),
                 ft = *(unsigned *)(p + 16);
        if (!oft && !namerva && !ft) break;
        const char *dll = (const char *)(d + namerva);
        HMODULE h = LoadLibraryA(dll);
        if (!h) {
            printf("[MOD  FAIL] %s (GLE=%lu)\n", dll, GetLastError());
            bad++; continue;
        }
        unsigned *t = (unsigned *)(d + oft);
        for (int k = 0; t[k]; k++) {
            if (t[k] & 0x80000000) {
                if (!GetProcAddress(h, (LPCSTR)(t[k] & 0xffff))) {
                    printf("[ORD  FAIL] %s #%u (GLE=%lu)\n", dll, t[k] & 0xffff, GetLastError());
                    bad++;
                }
            } else {
                const char *fn = (const char *)(d + t[k] + 2);
                if (!GetProcAddress(h, fn)) {
                    printf("[NAME FAIL] %s!%s (GLE=%lu)\n", dll, fn, GetLastError());
                    bad++;
                }
            }
        }
    }
    printf(bad ? "TOTAL BAD: %d\n" : "ALL IMPORTS RESOLVABLE\n", bad);
    return bad ? 1 : 0;
}
