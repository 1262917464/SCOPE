/* tm_test.c — Tilemap B31/B51/B5 修复验证 (2026-11-08) */
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
static int fails;
static void q(FnEval ev, FnGetStr gs, const char *label, const char *expr, const char *want)
{
    char code[1024];
    sprintf(code, "$q = begin; %s; rescue Exception => e; 'EXC ' + e.class.to_s + ': ' + e.message.to_s; end", expr);
    ev(code);
    const char *got = gs("$q");
    int ok = strcmp(got, want) == 0;
    printf("%s %-24s => %s\n", ok ? "OK " : "FAIL", label, got);
    if (!ok) { printf("     want: %s\n", want); fails++; }
}
int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    setvbuf(stdout, NULL, _IONBF, 0);
    HMODULE h = LoadLibraryA(argv[1]);
    FnInit init = (FnInit)GetProcAddress(h, "RGSSInitialize2");
    FnEval ev = (FnEval)GetProcAddress(h, "RGSSEval");
    FnGetStr gs = (FnGetStr)GetProcAddress(h, "RGSSGetStringUTF8");
    init(0);
    {
        void *fp[2];
        HWND hw = CreateWindowA("STATIC", "t", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                0, 0, 544, 416, NULL, NULL, NULL, NULL);
        fp[0] = (void *)GetProcAddress(h, "RGSSGameMain");
        fp[1] = (void *)hw;
        HANDLE th = CreateThread(NULL, 0, runner, fp, 0, NULL);
        Sleep(5000);
        PostMessageA(hw, WM_CLOSE, 0, 0);
        WaitForSingleObject(th, 10000);
        CloseHandle(th);
        Sleep(500);
    }
    q(ev, gs, "no x method",      "$t = Tilemap.new; begin; $t.x; 'has'; rescue NoMethodError; 'none'; end", "none");
    q(ev, gs, "no opacity",       "begin; $t.opacity; 'has'; rescue NoMethodError; 'none'; end", "none");
    q(ev, gs, "no blend_type",    "begin; $t.blend_type; 'has'; rescue NoMethodError; 'none'; end", "none");
    q(ev, gs, "ox Fixnum",        "$t.ox.class.to_s", "Fixnum");
    q(ev, gs, "oy Fixnum",        "$t.oy.class.to_s", "Fixnum");
    q(ev, gs, "ox= roundtrip",    "$t.ox = 64; $t.ox.to_s", "64");
    q(ev, gs, "BA exists",        "Tilemap::BitmapArray.to_s", "Tilemap::BitmapArray");
    q(ev, gs, "BA default nil",   "$a = Tilemap::BitmapArray.new; $a[3].nil?.to_s", "true");
    q(ev, gs, "BA set+get",       "$a[3] = 42; $a[3].to_s", "42");
    q(ev, gs, "BA [] oob nil",    "$a[9].nil?.to_s", "true");
    q(ev, gs, "BA []= oob silent","$a[9] = 1; $a[9].nil?.to_s", "true");
    q(ev, gs, "BA idx8 ok",       "$a[8] = 7; $a[8].to_s", "7");
    q(ev, gs, "bitmaps default",  "$t.bitmaps.class.to_s", "Tilemap::BitmapArray");
    q(ev, gs, "no bitmaps=",     "begin; $t.bitmaps = nil; 'has'; rescue NoMethodError; 'none'; end", "none");
    q(ev, gs, "dup raises",       "begin; $t.dup; 'no'; rescue TypeError => e; e.message; end", "can't dup Tilemap");
    q(ev, gs, "clone raises",     "begin; $t.clone; 'no'; rescue TypeError => e; e.message; end", "can't clone Tilemap");
    q(ev, gs, "disposed check",   "$t.dispose; begin; $t.ox; 'no'; rescue RGSSError; 'raise'; end", "raise");
    printf("== %s == (%d fails)\n", fails ? "FAIL" : "ALL PASS", fails);
    TerminateProcess(GetCurrentProcess(), 0);
    return 0;
}
