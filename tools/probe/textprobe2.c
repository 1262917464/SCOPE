/* textprobe2.c — 随真窗口跑 GameMain, 轮询到 Bitmap 注册后再逐步 eval
 * 用法: textprobe2.exe <游戏目录> <dll文件名> <输出.bin> */
#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef int (*FnInit)(int);
typedef int (*FnEval)(const char *);
typedef int (*FnGetStr)(const char *);

static HMODULE g_h;
static FnEval g_ev;
static FnGetStr g_gs;
static volatile LONG g_stop = 0;

static DWORD WINAPI gamemain(LPVOID p)
{
    typedef int (*F)(HWND, int, int);
    F m = (F)GetProcAddress(g_h, "RGSSGameMain");
    m((HWND)(DWORD_PTR)p, 0, 0);
    return 0;
}

static BOOL WINAPI on_close(DWORD t)
{
    InterlockedExchange(&g_stop, 1);
    return TRUE;
}

/* 轮询直到 expr 为真或超时 */
static int wait_until(const char *expr, int timeout_ms)
{
    char code[512];
    int waited = 0;
    sprintf(code, "$q=(%s)", expr);
    while (waited < timeout_ms) {
        if (g_stop) return 0;
        g_ev(code);
        if (strcmp(g_gs("$q"), "true") == 0) return 1;
        Sleep(100);
        waited += 100;
    }
    return 0;
}

static void step(const char *code)
{
    g_ev(code);
    const char *r = g_gs("($m||'').to_s");
    printf("  %s\n", r ? r : "(?)");
}

int main(int argc, char **argv)
{
    char cwd[1024], cmd[512], dll[1200];
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    FnInit init;
    HMODULE h;
    int i;

    if (argc < 4) { printf("usage: textprobe2 <dir> <dll> <out>\n"); return 2; }
    setvbuf(stdout, NULL, _IONBF, 0);

    /* 在游戏目录里启动 Game.exe 并注入本探针为线程? —— 不可行(跨进程)。
     * 改为: 本进程内 LoadLibrary + 自建窗口 + GameMain(与 difftest 同源)。 */
    sprintf(dll, "%s\\%s", argv[1], argv[2]);
    h = LoadLibraryA(dll);
    if (!h) { printf("LoadLibrary fail %lu\n", GetLastError()); return 1; }
    g_h  = h;
    init = (FnInit)GetProcAddress(h, "RGSSInitialize2");
    g_ev = (FnEval)GetProcAddress(h, "RGSSEval");
    g_gs = (FnGetStr)GetProcAddress(h, "RGSSGetStringUTF8");
    init(0);

    SetConsoleCtrlHandler(on_close, TRUE);

    {
        void *fp[1];
        HANDLE th;
        HWND hw = CreateWindowA("STATIC", "Project3", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                0, 0, 544, 416, NULL, NULL, NULL, NULL);
        fp[0] = (void *)hw;
        th = CreateThread(NULL, 0, gamemain, fp, 0, NULL);
        (void)th;
    }

    if (!wait_until("defined?(Bitmap)", 20000)) {
        printf("Bitmap never registered\n");
        return 1;
    }
    printf("Bitmap registered\n");

    step("begin; $b=Bitmap.new(220,80); $m='bm'; rescue Exception=>$e; $m='EXC:'+$e.message; end");
    step("begin; $b.fill_rect(0,0,220,80,Color.new(40,80,160,255)); $m='fill'; rescue Exception=>$e; $m='EXC:'+$e.message; end");
    step("begin; $b.font.size=22; $m='size'; rescue Exception=>$e; $m='EXC:'+$e.message; end");
    step("begin; $b.font.name=[\"\xE9\x9C\x9E\xE9\xB9\x9C\xE6\x96\x87\xE6\xA5\xB7 \xE5\xB1\x8F\xE5\xB9\x95\xE9\x98\x85\xE8\xAF\xBB\xE7\x89\x88 R\"]; $m='name'; rescue Exception=>$e; $m='EXC:'+$e.message; end");
    step("begin; $b.font.color=Color.new(255,255,255,255); $m='color'; rescue Exception=>$e; $m='EXC:'+$e.message; end");
    step("begin; $b.font.shadow=true; $b.draw_text(4,6,210,30,\"\xE7\x89\xB9\xE6\xAE\x8A\xE6\x8A\x80\xE8\x83\xBD\",0); $m='draw1'; rescue Exception=>$e; $m='EXC:'+$e.message; end");
    step("begin; $b.font.shadow=false; $b.draw_text(4,40,210,30,\"\xE7\x8B\x80\xE6\x85\x8B\xE8\xA3\x9D\xE5\x82\x99\",0); $m='draw2'; rescue Exception=>$e; $m='EXC:'+$e.message; end");
    step("begin; $px=[220,80].pack('l*');"
         "80.times{|y| 220.times{|x| c=$b.get_pixel(x,y);"
         "$px << c.red.to_i.chr << c.green.to_i.chr << c.blue.to_i.chr << c.alpha.to_i.chr}}; $m='px';"
         "rescue Exception=>$e; $m='EXC:'+$e.message; end");
    {
        char io[600];
        sprintf(io, "begin; File.open(\"%s\",\"wb\"){|f| f.write($px)}; $m='saved';"
                    "rescue Exception=>$e; $m='EXC:'+$e.message; end", argv[3]);
        step(io);
    }
    printf("done\n");
    TerminateProcess(GetCurrentProcess(), 0);
    return 0;
}
