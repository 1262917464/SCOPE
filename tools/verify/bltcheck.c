/* bltcheck.c — blt / stretch_blt 黑盒对拍探针
 * 用法: bltcheck.exe <RGSS202E.dll 路径>
 *
 * 调用序列照抄 Game.exe 逆向 (tools/verify/game_exe_调用序列.md):
 *   RegisterClass("RGSS Player") → CreateWindowW(未显示, 0x100)
 *   → RGSSInitialize2(hwnd) → RGSSSetupRTP(...) → RGSSSetupFonts(...)
 *   → SetWindowPos/ShowWindow(SW_SHOW) → GameMain 线程 (hwnd)
 *   → 等脚本收工后在本线程排队 eval (仿 Game.exe WAIT_TIMEOUT 模式,
 *     eval 在 GameMain 线程内跑, 避免 b19test 时代的跨线程竞态崩溃)
 * 关窗: PostMessageW(WM_CLOSE) → 等线程退出 (0x4019cb 的 ExitProcess 路径
 * 不会发生, 因为等待的是线程而不是进程)
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef int  (__cdecl *FnInit)(int);
typedef int  (__cdecl *FnEval)(const char *);
typedef const char * (__cdecl *FnGetStr)(const char *);
typedef int  (__cdecl *FnSetupRTP)(void *, void *, int);
typedef int  (__cdecl *FnSetupFonts)(void *, void *, int);
typedef int  (__cdecl *FnGameMain)(int, int, int);

static FnEval   g_eval;
static FnGetStr g_getstr;
static HWND     g_hwnd;

struct evalq { char code[4096]; };
static struct evalq g_q[64];
static int g_qn;

/* ---- GameMain 线程: 主循环 + 消息泵 + eval 排队消费 (照抄 Game.exe) ---- */
static DWORD WINAPI game_thread(LPVOID p)
{
    FnGameMain gm = (FnGameMain)((void **)p)[0];
    int hwnd = (int)(DWORD_PTR)((void **)p)[1];

    gm(hwnd, 0, 0);

    /* GameMain 已返回 (脚本报错/收工) — 但类仍已注册, 排队的 eval 在这里消费 */
    MSG msg;
    while (g_qn > 0) {
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        g_eval(g_q[0].code);
        memmove(&g_q[0], &g_q[1], sizeof(struct evalq) * (g_qn - 1));
        g_qn--;
    }
    return 0;
}

/* 主线程排队 eval; eval 真正发生在 GameMain 线程 (等它跑到消费循环) */
static void t(const char *label, const char *code)
{
    if (g_qn >= 63) { printf("%-52s (queue full)\n", label); return; }
    char buf[4096];
    sprintf(buf, "$__r = begin; %s; rescue Exception => e; \"ERR: #{e.class}: #{e.message}\"; end", code);
    lstrcpynA(g_q[g_qn].code, buf, 4096);
    g_qn++;
}

static LONG WINAPI crash_filter(EXCEPTION_POINTERS *ep)
{
    HMODULE h = GetModuleHandleA("RGSS202E.dll");
    DWORD rel = 0;
    if (h && (DWORD_PTR)ep->ExceptionRecord->ExceptionAddress > (DWORD_PTR)h)
        rel = (DWORD)((char *)ep->ExceptionRecord->ExceptionAddress - (char *)h);
    printf("\n*** CRASH 0x%08lX (RGSS202E+0x%08lX) code=0x%08lX\n",
           (unsigned long)(DWORD_PTR)ep->ExceptionRecord->ExceptionAddress,
           (unsigned long)rel,
           (unsigned long)ep->ExceptionRecord->ExceptionCode);
    fflush(stdout);
    ExitProcess(140);
}

int main(int argc, char **argv)
{
    HMODULE h;
    FnInit init;
    FnSetupRTP rtp;
    FnSetupFonts fonts;

    if (argc < 2) { printf("usage: bltcheck <dll>\n"); return 1; }
    setvbuf(stdout, NULL, _IONBF, 0);
    SetUnhandledExceptionFilter(crash_filter);
    h = LoadLibraryA(argv[1]);
    if (!h) { printf("LoadLibrary failed %lu\n", GetLastError()); return 1; }
    init   = (FnInit)GetProcAddress(h, "RGSSInitialize2");
    g_eval = (FnEval)GetProcAddress(h, "RGSSEval");
    g_getstr = (FnGetStr)GetProcAddress(h, "RGSSGetStringUTF8");
    rtp    = (FnSetupRTP)GetProcAddress(h, "RGSSSetupRTP");
    fonts  = (FnSetupFonts)GetProcAddress(h, "RGSSSetupFonts");
    if (!init || !g_eval) { printf("missing exports\n"); return 1; }

    /* ---- 照抄 Game.exe: 窗口先行 ---- */
    WNDCLASSA wc;
    memset(&wc, 0, sizeof wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;   /* 8 */
    wc.lpfnWndProc = DefWindowProcA;
    wc.hCursor = LoadCursorA(NULL, MAKEINTRESOURCEA(0x7F00));
    wc.lpszClassName = "RGSS Player";
    if (!RegisterClassA(&wc)) { printf("RegisterClass failed\n"); return 1; }
    /* 照抄 Game.exe 0x401652: WS_EX_WINDOWEDGE + style 0xCA0000 +
     * CW_USEDEFAULT x/y + 0x0 尺寸 (后由 AdjustWindowRectEx 定 544x416) */
    HWND hwnd = CreateWindowExA(0x100, "RGSS Player", "bltcheck", 0xCA0000,
                                0x80000000, 0x80000000, 0, 0, NULL, NULL, NULL, NULL);
    if (!hwnd) { printf("CreateWindow failed %lu\n", GetLastError()); return 1; }
    { RECT rc = {0, 0, 544, 416};
      AdjustWindowRectEx(&rc, 0xCA0000, FALSE, 0x100);
      SetWindowPos(hwnd, NULL, 0, 0, rc.right-rc.left, rc.bottom-rc.top, 0x14); }

    init((int)(DWORD_PTR)hwnd);
    /* Game.exe 实测 (0x4018ae/0x4019b0): 两函数都是 3 参, 传 ini 读出的
     * 宽字符串缓冲与容量; RTP 为空时传 L"" (0x3150 缓冲由 0x4081b8=L"" 拷贝) */
    {
        static wchar_t empty[2] = L"";
        static wchar_t buf2[512], buf3[512];
        rtp(empty, buf2, 0x400);
        fonts(empty, buf3, 0x400);
    }
    SetWindowPos(hwnd, 0, 0, 0, 0, 0, 0x14);
    ShowWindow(hwnd, SW_SHOW);

    /* GameMain 线程 */
    static void *fp[2];
    fp[0] = (void *)GetProcAddress(h, "RGSSGameMain");
    fp[1] = (void *)hwnd;
    HANDLE th = CreateThread(NULL, 0, game_thread, fp, 0, NULL);
    printf("GameMain warmup 7s (等待脚本加载+phase B)...\n");
    Sleep(7000);
    /* 关窗 → GameMain 经 Graphics.update 的关闭检查 eval("exit 0") 收工 */
    PostMessageA(hwnd, WM_CLOSE, 0, 0);
    Sleep(4000);   /* 等线程进入消费循环 */

    /* ---- 测试矩阵 (排队, GameMain 线程消费) ---- */
    puts("=== P1 blt 基础 (dst A=255) ===");
    t("setup",
      "$d = Bitmap.new(4,1); $d.fill_rect(0,0,4,1,Color.new(100,150,200,255));"
      "$s = Bitmap.new(4,1);"
      "$s.set_pixel(0,0,Color.new(255,0,0,255));"
      "$s.set_pixel(1,0,Color.new(0,255,0,128));"
      "$s.set_pixel(2,0,Color.new(0,0,255,64));"
      "$s.set_pixel(3,0,Color.new(9,9,9,0)); 'ok'");
    t("dump util", "$dump = lambda { |bm| (0...bm.width).map { |x| bm.get_pixel(x,0).inspect }.join(' ') }; 'ok'");
    t("blt op255", "$d.blt(0,0,$s,$s.rect,255); $dump.call($d)");
    puts("=== P2 blt op 递进 ===");
    t("blt op128", "$d.blt(0,0,$s,$s.rect,128); $dump.call($d)");
    t("blt op64",  "$d.blt(0,0,$s,$s.rect,64);  $dump.call($d)");
    t("blt op0",   "$d.blt(0,0,$s,$s.rect,0);   $dump.call($d)");
    puts("=== P3 blt dst alpha=0 ===");
    t("setup d2", "$d2 = Bitmap.new(4,1); $d2.fill_rect(0,0,4,1,Color.new(100,150,200,0)); 'ok'");
    t("blt op255 dstA0", "$d2.blt(0,0,$s,$s.rect,255); $dump.call($d2)");
    t("blt op128 dstA0", "$d2.blt(0,0,$s,$s.rect,128); $dump.call($d2)");
    puts("=== P4 stretch 放大 2x (4->8) ===");
    t("setup sb", "$d3 = Bitmap.new(8,1); $d3.fill_rect(0,0,8,1,Color.new(50,60,70,255)); $r8 = Rect.new(0,0,8,1); 'ok'");
    t("sb 4to8 op255", "$d3.stretch_blt($r8,$s,$s.rect,255); $dump.call($d3)");
    t("sb 4to8 op128", "$d3.fill_rect(0,0,8,1,Color.new(50,60,70,255)); $d3.stretch_blt($r8,$s,$s.rect,128); $dump.call($d3)");
    puts("=== P5 stretch 缩小/放大 (8 -> 5/4/16) ===");
    t("setup s2", "$s2 = Bitmap.new(8,1); (0..7).each { |x| $s2.set_pixel(x,0,Color.new(x*30,255-x*30,128,255)) }; 'ok'");
    t("sb 8to5", "$d4 = Bitmap.new(5,1); $d4.stretch_blt($d4.rect,$s2,$s2.rect,255); $dump.call($d4)");
    t("sb 8to4", "$d5 = Bitmap.new(4,1); $d5.stretch_blt($d5.rect,$s2,$s2.rect,255); $dump.call($d5)");
    t("sb 8to16", "$d6 = Bitmap.new(16,1); $d6.stretch_blt($d6.rect,$s2,$s2.rect,255); $dump.call($d6)");
    puts("=== P6 stretch 纵向 (1x4 -> 1x8) ===");
    t("setup v", "$sv = Bitmap.new(1,4); (0..3).each { |y| $sv.set_pixel(0,y,Color.new(255,y*60,0,255)) }; 'ok'");
    t("sb v 4to8", "$dv = Bitmap.new(1,8); $dv.stretch_blt($dv.rect,$sv,$sv.rect,255); (0..7).map { |y| $dv.get_pixel(0,y).inspect }.join(' ')");
    puts("=== P7 类型错误 ===");
    t("blt 非Bitmap", "begin; $d.blt(0,0,42,$s.rect,255); 'noraise'; rescue Exception; 'raised '+$!.class.to_s; end");

    puts("done");
    fflush(stdout);
    TerminateProcess(GetCurrentProcess(), 0);
    return 0;
}
