/* host.c — M3 验收宿主: 加载 ruby_test.dll 跑 eval 电池 */
#include <windows.h>
#include <stdio.h>

typedef void (*FnInit)(void);
typedef int (*FnEval)(const char *);
typedef const char *(*FnEvalStr)(const char *);

struct { const char *code; const char *want; } cases[] = {
    { "1+1",                              "2" },
    { "RUBY_VERSION",                     "\"1.8.1\"" },
    { "'a' + 'b'",                        "\"ab\"" },
    { "(1..10).inject { |s, x| s + x }",  "55" },
    { "%w(a b c).map { |x| x.upcase }.join",
                                         "\"ABC\"" },
    { "Marshal.dump(42).unpack('H*')[0]", "\"0408692f\"" },
    { "def f(n); n*2; end; f(21)",        "42" },
    { "'hoge'.sub(/o/,'0')",              "\"h0ge\"" },
    { "$test_nil = nil; $test_nil.inspect", "\"nil\"" },
    { "raise 'boom'",                     "RuntimeError: (eval)boom" },
};

int main(int argc, char **argv)
{
    HMODULE h = LoadLibraryA(argv[1]);
    if (!h) { fprintf(stderr, "load fail %lu\n", GetLastError()); return 1; }
    FnInit init = (FnInit)GetProcAddress(h, "RubyInit");
    FnEval ev = (FnEval)GetProcAddress(h, "RubyEval");
    FnEvalStr evs = (FnEvalStr)GetProcAddress(h, "RubyEvalStr");
    if (!init || !ev || !evs) { fprintf(stderr, "exports missing\n"); return 1; }
    init();
    int pass = 0, total = sizeof cases / sizeof cases[0];
    for (int i = 0; i < total; i++) {
        const char *got = evs(cases[i].code);
        int ok = strcmp(got, cases[i].want) == 0;
        printf("%-2d %-44s => %-28s %s\n", i, cases[i].code, got, ok ? "PASS" : "FAIL(expect)");
        if (ok) pass++;
        else printf("     expected: %s\n", cases[i].want);
    }
    printf("== %d/%d ==\n", pass, total);
    /* RubyEval 快速通道验证 */
    printf("RubyEval(\"6*7\") = %d (want 42)\n", ev("6*7"));
    return pass == total ? 0 : 1;
}
/* appended: Win32API 用例(编译期追加) */
