/* b19test.c — B19 验证: dispose 后访问器 raise RGSSError
 * phaseb 模式: RGSSInitialize2 → GameMain 线程(阶段B注册) → RGSSEval 断言电池。
 */
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

static int fails;
static void q(FnEval ev, FnGetStr gs, const char *label, const char *expr, const char *want)
{
    char code[1024];
    sprintf(code, "$q=(%s).to_s", expr);
    ev(code);
    const char *got = gs("$q");
    int ok = strcmp(got, want) == 0;
    printf("%-2s %-46s => %-10s %s\n", ok ? "OK" : "FAIL", label, got,
           ok ? "" : want);
    if (!ok) fails++;
}

int main(int argc, char **argv)
{
    HMODULE h; FnInit init; FnEval ev; FnGetStr gs;
    if (argc < 2) return 2;
    setvbuf(stdout, NULL, _IONBF, 0);
    h = LoadLibraryA(argv[1]);
    if (!h) { printf("load fail\n"); return 1; }
    init = (FnInit)GetProcAddress(h, "RGSSInitialize2");
    ev   = (FnEval)GetProcAddress(h, "RGSSEval");
    gs   = (FnGetStr)GetProcAddress(h, "RGSSGetStringUTF8");
    init(0);
    {
        void *fp[2];
        HWND hw = CreateWindowA("STATIC", "p", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                0, 0, 544, 416, NULL, NULL, NULL, NULL);
        fp[0] = (void *)GetProcAddress(h, "RGSSGameMain");
        fp[1] = (void *)hw;
        { HANDLE th = CreateThread(NULL, 0, runner, fp, 0, NULL);
          Sleep(6000);
          /* 关窗让游戏线程 SystemExit 收工 (RGSSEval 与脚本并发是原版契约外
           * 行为, 会竞态)。阶段 B 注册的类在进程内保持已定义。 */
          if (hw) PostMessageA(hw, WM_CLOSE, 0, 0);
          WaitForSingleObject(th, 10000);
          CloseHandle(th);
          Sleep(500); }
    }
    q(ev, gs, "sprite disposed x raises",
      "begin; $s=Sprite.new; $s.dispose; $s.x; 'noraise'; rescue RGSSError; 'raise'; end", "raise");
    q(ev, gs, "sprite disposed? true",
      "$s.disposed?.to_s", "true");
    q(ev, gs, "sprite double-dispose safe",
      "begin; $s.dispose; 'ok'; rescue Exception; 'raise'; end", "ok");
    q(ev, gs, "window disposed y raises",
      "begin; $w=Window.new; $w.dispose; $w.x; 'noraise'; rescue RGSSError; 'raise'; end", "raise");
    q(ev, gs, "viewport disposed ox raises",
      "begin; $v=Viewport.new(0,0,8,8); $v.dispose; $v.ox; 'noraise'; rescue RGSSError; 'raise'; end", "raise");
    q(ev, gs, "plane disposed z raises",
      "begin; $p=Plane.new; $p.dispose; $p.z; 'noraise'; rescue RGSSError; 'raise'; end", "raise");
    q(ev, gs, "tilemap disposed ox raises",
      "begin; $t=Tilemap.new; $t.dispose; $t.ox; 'noraise'; rescue RGSSError; 'raise'; end", "raise");
    q(ev, gs, "bitmap disposed? after dispose",
      "$b=Bitmap.new(16,16); $b.dispose; $b.disposed?.to_s", "true");
    q(ev, gs, "bitmap disposed width raises",
      "begin; $b.width; 'noraise'; rescue RGSSError; 'raise'; end", "raise");
    q(ev, gs, "live sprite x no raise",
      "begin; Sprite.new.x; 'ok'; rescue RGSSError; 'raise'; end", "ok");
    q(ev, gs, "live bitmap width no raise",
      "begin; Bitmap.new(16,16).width; 'ok'; rescue RGSSError; 'raise'; end", "ok");
    printf("== %s ==\n", fails ? "FAIL" : "ALL PASS");
    TerminateProcess(GetCurrentProcess(), 0);
    return 0;
}
