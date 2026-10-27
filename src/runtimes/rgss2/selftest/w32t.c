/* w32t.c — Win32API 扩展接入验证 */
#include <windows.h>
#include <stdio.h>

typedef void (*FnV)(void);
typedef const char *(*FnEvalStr)(const char *);

int main(int argc, char **argv)
{
    HMODULE h = LoadLibraryA(argv[1]);
    if (!h) { fprintf(stderr, "load fail\n"); return 1; }
    FnV init = (FnV)GetProcAddress(h, "RubyInit");
    FnV initw = (FnV)GetProcAddress(h, "RubyInitWin32API");
    FnEvalStr evs = (FnEvalStr)GetProcAddress(h, "RubyEvalStr");
    if (!init || !initw || !evs) { fprintf(stderr, "exports missing\n"); return 1; }
    init(); initw();
    const char *cases[] = {
        "Win32API.new('kernel32','GetTickCount','V','L').call > 0",
        "a = Win32API.new('kernel32','GetVersion','V','L').call; (a & 0xFFFF).to_s",
        "defined?(Win32API).to_s",
    };
    const char *want[] = { "true", "\"6\"|\"10\"|\"11\"|\"15\"|\"14\"", "constant" };
    for (int i = 0; i < 3; i++)
        printf("%-58s => %s\n", cases[i], evs(cases[i]));
    return 0;
}
