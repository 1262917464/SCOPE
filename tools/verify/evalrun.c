/* evalrun.c — 通用进程内 eval: evalrun.exe <dll> <code.rb>  (cwd 任意)
 * Ruby 侧: eval 代码本身负责落盘/打印。GameMain 线程跑空转，eval 在 1.5s 后发出。 */
#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

typedef int (*FnInit)(int);
typedef int (*FnEval)(const char *);

static DWORD WINAPI runner(LPVOID p)
{
    typedef int (*F)(int, int, int);
    F m = (F)((void **)p)[0];
    m((int)(DWORD_PTR)((void **)p)[1], 0, 0);
    return 0;
}

int main(int argc, char **argv)
{
    HMODULE h; FnInit init; FnEval ev; void *fp[2]; HWND hw; HANDLE th; FILE *f;
    long len; char *code; int rc;
    if (argc < 3) return 2;
    setvbuf(stdout, NULL, _IONBF, 0);
    f = fopen(argv[2], "rb");
    if (!f) { printf("no code file\n"); return 1; }
    fseek(f, 0, SEEK_END); len = ftell(f); fseek(f, 0, SEEK_SET);
    code = malloc(len + 16);
    memset(code, 0, len + 16);
    fread(code, 1, len, f);
    fclose(f);
    h = LoadLibraryA(argv[1]);
    if (!h) { printf("load fail\n"); return 1; }
    init = (FnInit)GetProcAddress(h, "RGSSInitialize2");
    ev   = (FnEval)GetProcAddress(h, "RGSSEval");
    init(0);
    hw = CreateWindowA("STATIC", "g", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                       0, 0, 544, 416, NULL, NULL, NULL, NULL);
    fp[0] = (void *)GetProcAddress(h, "RGSSGameMain");
    fp[1] = (void *)hw;
    th = CreateThread(NULL, 0, runner, fp, 0, NULL);
    Sleep(1500);
    rc = ev(code);
    printf("eval rc=%d\n", rc);
    if (hw) PostMessageA(hw, WM_CLOSE, 0, 0);
    WaitForSingleObject(th, 5000);
    return 0;
}
