/* oem_eval.c — 加载原版 RGSS202E.dll 跑 eval 对拍 */
#include <windows.h>
#include <stdio.h>

typedef int (*FnInit)(int);
typedef int (*FnEval)(const char *);
typedef int (*FnGetInt)(const char *);
typedef const char *(*FnGetStr)(const char *);

int main(int argc, char **argv)
{
    HMODULE h = LoadLibraryA(argv[1]);
    if (!h) { fprintf(stderr, "load fail %lu\n", GetLastError()); return 1; }
    FnInit init = (FnInit)GetProcAddress(h, "RGSSInitialize2");
    FnGetInt gi = (FnGetInt)GetProcAddress(h, "RGSSGetInt");
    FnEval ev = (FnEval)GetProcAddress(h, "RGSSEval");
    FnGetStr gs = (FnGetStr)GetProcAddress(h, "RGSSGetStringUTF8");
    printf("init=%p getint=%p eval=%p getstr=%p\n", (void*)init, (void*)gi, (void*)ev, (void*)gs);
    if (!init || !gi || !gs) return 1;
    int r = init(0);
    printf("RGSSInitialize2(0) = %d\n", r);
    struct { const char *c; } cases[] = {
        { "1+1" }, { "RUBY_VERSION" }, { "6*7" },
        { "(1..10).inject{ |s,x| s+x }" }, { "Marshal.dump(42).unpack('H*')[0]" },
    };
    for (int i = 0; i < 5; i++) {
        int rc = ev(cases[i].c);
        printf("%-36s eval_rc=%d getint=%d getstr=%s\n",
               cases[i].c, rc, gi(cases[i].c), gs(cases[i].c));
    }
    return 0;
}
