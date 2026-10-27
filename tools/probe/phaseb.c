/* phaseb.c — 探明原版 DLL 里 Bitmap/Font 在哪个阶段注册 */
#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <stdio.h>

typedef int (*FnInit)(int);
typedef int (*FnEval)(const char *);
typedef int (*FnGetStr)(const char *);

static DWORD WINAPI runner(LPVOID p)
{
    typedef int (*F)(int, int, int);
    F m = (F)((void **)p)[0];
    m((int)(DWORD_PTR)((void **)p)[1], 0, 0);
    return 0;
}

static void q(FnEval ev, FnGetStr gs, const char *label, const char *expr)
{
    char code[512];
    sprintf(code, "$q=(%s).to_s", expr);
    ev(code);
    printf("%-40s => %s\n", label, gs("$q"));
}

int main(int argc, char **argv)
{
    HMODULE h;
    FnInit init;
    FnEval ev;
    FnGetStr gs;

    if (argc < 2) return 2;
    setvbuf(stdout, NULL, _IONBF, 0);
    h = LoadLibraryA(argv[1]);
    init = (FnInit)GetProcAddress(h, "RGSSInitialize2");
    ev   = (FnEval)GetProcAddress(h, "RGSSEval");
    gs   = (FnGetStr)GetProcAddress(h, "RGSSGetStringUTF8");
    init(0);
    printf("=== after Initialize2 ===\n");
    q(ev, gs, "defined?(Bitmap)",  "defined?(Bitmap)");
    q(ev, gs, "defined?(Font)",    "defined?(Font)");
    q(ev, gs, "defined?(Table)",   "defined?(Table)");
    q(ev, gs, "defined?(Graphics)","defined?(Graphics)");

    puts("=== after Graphics.update ===");
    ev("begin; Graphics.update; rescue Exception=>$e; end");
    q(ev, gs, "Graphics.update exc", "$e.to_s");
    q(ev, gs, "defined?(Bitmap)",  "defined?(Bitmap)");
    q(ev, gs, "defined?(Font)",    "defined?(Font)");

    puts("=== after GameMain thread (6s) ===");
    {
        void *fp[2];
        HWND hw = CreateWindowA("STATIC", "p", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                0, 0, 544, 416, NULL, NULL, NULL, NULL);
        fp[0] = (void *)GetProcAddress(h, "RGSSGameMain");
        fp[1] = (void *)hw;
        CreateThread(NULL, 0, runner, fp, 0, NULL);
        Sleep(6000);
    }
    q(ev, gs, "defined?(Bitmap)",  "defined?(Bitmap)");
    q(ev, gs, "defined?(Font)",    "defined?(Font)");
    q(ev, gs, "Bitmap.instance_methods(false).size",
      "defined?(Bitmap) ? Bitmap.instance_methods(false).size.to_s : 'n/a'");

    fflush(stdout);
    TerminateProcess(GetCurrentProcess(), 0);
    return 0;
}
