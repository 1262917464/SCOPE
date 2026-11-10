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
int main(int argc, char **argv)
{
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
    puts("step1: class defined?");
    ev("$q = Tilemap::BitmapArray.to_s");
    puts(gs("$q"));
    puts("step2: new");
    ev("$q = Tilemap::BitmapArray.new.to_s");
    puts(gs("$q"));
    puts("step3: set+get");
    ev("$a = Tilemap::BitmapArray.new; $a[3] = 42; $q = $a[3].to_s");
    puts(gs("$q"));
    puts("step4: ox=");
    ev("$t = Tilemap.new; $t.ox = 64; $q = $t.ox.to_s");
    puts(gs("$q"));
    puts("step5: dup");
    ev("$q = begin; $t.dup; 'no'; rescue TypeError; 'raise'; end");
    puts(gs("$q"));
    puts("done");
    TerminateProcess(GetCurrentProcess(), 0);
    return 0;
}
