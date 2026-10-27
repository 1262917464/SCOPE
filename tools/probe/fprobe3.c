/* fprobe3.c — 用原版 DLL 实测 Font 各属性默认值 (需 Phase B) */
#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <stdio.h>

typedef int (*FnInit)(int);
typedef int (*FnEval)(const char *);
typedef int (*FnGetStr)(const char *);
typedef int (*FnMain)(int, int, int);

static DWORD WINAPI run_main(LPVOID p)
{
    FnMain m = (FnMain)((void **)p)[0];
    m((int)(DWORD_PTR)((void **)p)[1], 0, 0);
    return 0;
}

static const char *EVAL_CODE =
    "$out=[]; begin;"
    " f=Font.new; "
    " $out << 'shadow='+f.shadow.inspect;"
    " $out << 'bold='+f.bold.inspect;"
    " $out << 'italic='+f.italic.inspect;"
    " $out << 'size='+f.size.inspect;"
    " $out << 'color='+f.color.inspect;"
    " $out << 'name='+f.name.inspect;"
    " $out << 'default_shadow='+Font.default_shadow.inspect;"
    " $out << 'default_bold='+Font.default_bold.inspect;"
    " $out << 'default_size='+Font.default_size.inspect;"
    " $out << 'default_color='+Font.default_color.inspect;"
    " $out << 'default_name='+Font.default_name.inspect;"
    " rescue Exception=>e; $out << 'EXC '+e.class.to_s+': '+e.message; end;"
    "File.open('fontdump.txt','wb'){|f| f.puts($out)}";

int main(int argc, char **argv)
{
    HMODULE h;
    FnInit init; FnEval ev; FnGetStr gs;
    void *fp[2]; HANDLE th; HWND hw;
    char code[65536]; FILE *f; long n;

    if (argc < 2) return 2;
    h = LoadLibraryA(argv[1]);
    if (!h) return 1;
    init = (FnInit)GetProcAddress(h, "RGSSInitialize2");
    ev   = (FnEval)GetProcAddress(h, "RGSSEval");
    gs   = (FnGetStr)GetProcAddress(h, "RGSSGetStringUTF8");
    if (!init || !ev || !gs) return 1;
    init(0);

    hw = CreateWindowA("STATIC", "p", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                       0, 0, 544, 416, NULL, NULL, NULL, NULL);
    fp[0] = (void *)GetProcAddress(h, "RGSSGameMain");
    fp[1] = (void *)hw;
    th = CreateThread(NULL, 0, run_main, fp, 0, NULL);
    Sleep(8000);

    ev(EVAL_CODE);
    Sleep(1500);
    f = fopen("fontdump.txt", "rb");
    if (f) { n = fread(code, 1, sizeof(code) - 1, f); code[n] = 0; fclose(f); printf("%s\n", code); }
    else printf("no file\n");
    return 0;
}
