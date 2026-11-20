/* tdump.c — B1 修复验证：Table _dump/_load 回环 + 头部布局对拍
 * 用法: tdump.exe <RGSS202E_rebuild.dll> [<原版dll>]
 * 依据: 原版 0x1000ec20 _dump 头 5 字段 = [维度数,x,y,z,n]（2026-11-20 反汇编确证） */
#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <stdio.h>

typedef int (*FnInit)(int);
typedef int (*FnEval)(const char *);
typedef const char *(*FnGetStr)(const char *);

static DWORD WINAPI runner(LPVOID p)
{
    typedef int (*F)(int, int, int);
    F m = (F)((void **)p)[0];
    m((int)(DWORD_PTR)((void **)p)[1], 0, 0);
    return 0;
}

static FnEval ev;
static FnGetStr gs;

static int q(const char *expr)   /* eval expr -> $q, 返回数值 */
{
    char code[512];
    int v = 0;
    int st;
    sprintf(code, "$q=(%s).to_s rescue $q='-888888'", expr);
    st = ev(code);
    if (st != 0) { printf("  [eval st=%d for: %s]\n", st, expr); return -777777; }
    if (sscanf(gs("$q"), "%d", &v) != 1) v = -999999;
    return v;
}

#define CHECK(label, got, want) \
    do { int g_=(got), w_=(want); \
         printf("%-46s got=%d want=%d %s\n", label, g_, w_, g_==w_?"PASS":"FAIL"); \
         if (g_!=w_) fails++; } while (0)

int main(int argc, char **argv)
{
    HMODULE h;
    FnInit init;
    int fails = 0;

    if (argc < 2) { fprintf(stderr, "usage: tdump <dll>\n"); return 2; }
    setvbuf(stdout, NULL, _IONBF, 0);
    h = LoadLibraryA(argv[1]);
    if (!h) { fprintf(stderr, "LoadLibrary FAILED GLE=%lu\n", GetLastError()); return 2; }
    init = (FnInit)GetProcAddress(h, "RGSSInitialize2");
    ev   = (FnEval)GetProcAddress(h, "RGSSEval");
    gs   = (FnGetStr)GetProcAddress(h, "RGSSGetStringUTF8");
    if (!init || !ev || !gs) { fprintf(stderr, "missing exports init=%p ev=%p gs=%p\n", (void*)init, (void*)ev, (void*)gs); return 2; }
    init(0);
    /* phaseb 模式: Table 在 GameMain 线程的 phase A 注册，必须先进 GameMain */
    {
        void *fp[2];
        HWND hw = CreateWindowA("STATIC", "p", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                0, 0, 544, 416, NULL, NULL, NULL, NULL);
        fp[0] = (void *)GetProcAddress(h, "RGSSGameMain");
        fp[1] = (void *)hw;
        { HANDLE th = CreateThread(NULL, 0, runner, fp, 0, NULL);
          Sleep(4000);
          if (hw) PostMessageA(hw, WM_CLOSE, 0, 0);
          WaitForSingleObject(th, 10000);
          CloseHandle(th);
          Sleep(500); }
    }

    /* 0. 前置健全性检查 */
    CHECK("defined?(Table) != 0", q("defined?(Table) == 'constant' ? 1 : 0"), 1);
    CHECK("Table.new(3,4).xsize", q("$x=Table.new(3,4); $x.xsize"), 3);

    /* 1. 2 维回环: 头字段错位时 xsize 会读成 ysize */
    ev("$t=Table.new(3,4); $t[1,2]=7; $d=$t._dump(0); $u=Table._load($d)");
    CHECK("2d roundtrip xsize", q("$u.xsize"), 3);
    CHECK("2d roundtrip ysize", q("$u.ysize"), 4);
    CHECK("2d roundtrip [1,2]",  q("$u[1,2]"),  7);
    CHECK("2d dump len(20+n*2)", q("$d.size"), 20 + 12*2);
    CHECK("2d header field1 = dims(2)", q("$d.unpack('l5')[0]"), 2);
    CHECK("2d header field2 = x(3)",    q("$d.unpack('l5')[1]"), 3);
    CHECK("2d header field5 = n(12)",   q("$d.unpack('l5')[4]"), 12);

    /* 2. 1 维 */
    ev("$a=Table.new(5); $a[3]=42; $b=Table._load($a._dump(0))");
    CHECK("1d roundtrip xsize", q("$b.xsize"), 5);
    CHECK("1d roundtrip [3]",   q("$b[3]"),   42);
    CHECK("1d header field1 = dims(1)", q("$a._dump(0).unpack('l5')[0]"), 1);

    /* 3. 3 维 */
    ev("$c=Table.new(2,3,4); $c[1,2,3]=9; $e=Table._load($c._dump(0))");
    CHECK("3d roundtrip zsize", q("$e.zsize"), 4);
    CHECK("3d roundtrip [1,2,3]", q("$e[1,2,3]"), 9);
    CHECK("3d header field1 = dims(3)", q("$c._dump(0).unpack('l5')[0]"), 3);

    /* 4. resize 更新维度数 */
    ev("$f=Table.new(6); $f.resize(6,2); $g=Table._load($f._dump(0))");
    CHECK("resize(6,2) roundtrip ysize", q("$g.ysize"), 2);
    CHECK("resize header dims=2", q("$f._dump(0).unpack('l5')[0]"), 2);

    printf(fails ? "== FAIL(%d) ==\n" : "== ALL PASS ==\n", fails);
    return fails ? 1 : 0;
}
