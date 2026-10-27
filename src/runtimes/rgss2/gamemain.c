/*
 * rgss_gamemain.c — RGSSGameMain 主循环实现（重建版）
 *
 * 依据 docs/M5_主循环与Init归属.md（sub_1000AFD0 全解）:
 *   - F12 = Reset 异常触发重启（外层 while(true)）
 *   - Game.ini 读 Scripts 路径 → $RGSS_SCRIPTS = load_data + Zlib::Inflate
 *   - 逐段 eval("'Section%03d'") + eval(src)
 *   - 正常退出: Graphics._reset;Input.update;Audio.*_stop;GC.start
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "ruby.h"

void rgss_register_phase_a(void);
void rgss_register_phase_b(void);
void rgss_graphics_set_hwnd(HWND h, int w, int hh);

static HWND  g_hwnd;
extern HWND  rgss_get_host_hwnd(void);
static int   g_running;
static int   g_reset_requested;
static char  g_scripts_fname[260];

/* ---------- 窗口过程 ---------- */
static LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_F12) g_reset_requested = 1;
        return 0;
    }
    return DefWindowProcA(h, msg, wp, lp);
}

static HWND create_game_window(const char *title, int w, int h)
{
    WNDCLASSA wc;
    ZeroMemory(&wc, sizeof wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wndproc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = "RGSSRebuildWindow";
    if (!RegisterClassA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return NULL;

    RECT rc = { 0, 0, w, h };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowA("RGSSRebuildWindow", title,
                              WS_OVERLAPPEDWINDOW,
                              CW_USEDEFAULT, CW_USEDEFAULT,
                              rc.right - rc.left, rc.bottom - rc.top,
                              NULL, NULL, wc.hInstance, NULL);
    if (hwnd) {
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
    }
    return hwnd;
}

/* ---------- Game.ini 读取 ---------- */
static void read_scripts_path(char *out, size_t outsz)
{
    out[0] = 0;
    char ini[MAX_PATH];
    GetModuleFileNameA(NULL, ini, MAX_PATH);
    char *p = strrchr(ini, '\\');
    if (!p) return;
    strcpy(p + 1, "Game.ini");
    GetPrivateProfileStringA("Game", "Scripts", "Data\\Scripts.rvdata",
                             out, (DWORD)outsz, ini);
}

/* ---------- 脚本装载与执行 ---------- */
static void logmsg(const char *fmt, ...)
{
    FILE *f = fopen("rgss_rebuild.log", "a");
    if (!f) return;
    va_list ap; va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc(10, f);
    fclose(f);
}

static int load_and_run_scripts(const char *fname)
{
    char code[4096];
    int state = 0;
    logmsg("[load] scripts=%s", fname);

    /* 1. 读入并解压 $RGSS_SCRIPTS */
    snprintf(code, sizeof code,
        "$RGSS_SCRIPTS = load_data('%s');"
        "$RGSS_SCRIPTS.each { |s| s[3,0] = Zlib::Inflate.inflate(s[2]) };"
        "$RGSS_SCRIPTS.size", fname);
    logmsg("[load] evaluating script loader");
    VALUE n = rb_eval_string_protect(code, &state);
    logmsg("[load] eval done state=%d", state);
    if (state) {
        VALUE err = rb_gv_get("$!");
        if (!NIL_P(err)) {
            VALUE msg = rb_funcall(err, rb_intern("message"), 0);
            char eb[2048];
            snprintf(eb, sizeof eb, "%s", StringValueCStr(msg));
            logmsg("[load] ERROR: %s", eb);
            MessageBoxA(g_hwnd, eb, "RGSS Error", MB_ICONWARNING);
        } else {
            logmsg("[load] state=%d but $! is nil", state);
        }
        return 0;
    }
    if (NIL_P(n)) { logmsg("[load] result is nil!"); return 0; }
    int count = NUM2INT(n);
    logmsg("[load] section count=%d", count);
    if (count <= 0) return 0;

    /* 2. 逐段执行 */
    for (int i = 0; i < count; i++) {
        snprintf(code, sizeof code, "'Section%03d'", i);
        rb_eval_string_protect(code, &state);
        if (state) continue;

        snprintf(code, sizeof code, "$RGSS_SCRIPTS[%d][3]", i);
        VALUE src = rb_eval_string_protect(code, &state);
        if (state || NIL_P(src)) continue;

        g_running = 1;
        logmsg("[exec] section %d (%d bytes)", i, (int)RSTRING(src)->len);
        if (i == 93) {
            /* 追踪: 每 10 帧记录一次 Graphics.update 调用 */
            int st2 = 0;
            rb_eval_string_protect(
                "$__gfx_calls = 0;"
                "module Kernel;"
                "  alias_method :__orig_gfx_update, :update if false;"
                "end;"
                "class << Graphics;"
                "  alias_method :__orig_update, :update;"
                "  def update; $__gfx_calls += 1;"
                "    if $__gfx_calls <= 3 || $__gfx_calls % 60 == 0;"
                "      File.open('rgss_trace.log','a'){|f| f.puts \"[gfx] #{$__gfx_calls}\"};"
                "    end;"
                "    __orig_update;"
                "  end;"
                "end;", &st2);
            logmsg("[trace] Graphics.update hooked, state=%d", st2);
        }
        if (i == 93) {
            fprintf(stderr, "[DBG] about to eval Main (%d bytes)\n", (int)RSTRING(src)->len);
            fflush(stderr);
        }
        rb_eval_string_protect(StringValueCStr(src), &state);
        if (i == 93) {
            fprintf(stderr, "[DBG] Main eval returned state=%d\n", state);
            fflush(stderr);
            if (state) {
                VALUE e = rb_gv_get("$!");
                VALUE m = NIL_P(e) ? rb_str_new2("(nil)") : rb_funcall(e, rb_intern("message"), 0);
                fprintf(stderr, "[DBG] Main error: %s\n", StringValueCStr(m));
                fflush(stderr);
            }
        }
        g_running = 0;
        /* 段 93 (Main) 之后检查场景状态 */
        if (i >= 92) {
            int s2 = 0;
            VALUE sc = rb_eval_string_protect("$scene.class.to_s rescue 'nil'", &s2);
            logmsg("[after] section %d state=%d $scene=%s", i, state,
                   (NIL_P(sc) ? "nil" : StringValueCStr(sc)));
            VALUE fc = rb_eval_string_protect("Graphics.frame_count.to_s rescue '?'", &s2);
            logmsg("[after] Graphics.frame_count=%s",
                   (NIL_P(fc) ? "nil" : StringValueCStr(fc)));
        }

        if (state) {
            VALUE err = rb_gv_get("$!");
            /* Reset 异常 → 重启 */
            if (!NIL_P(err)) {
                VALUE cls = rb_funcall(err, rb_intern("class"), 0);
                VALUE cname = rb_funcall(cls, rb_intern("to_s"), 0);
                if (strcmp(StringValueCStr(cname), "Reset") == 0) {
                    return 2;   /* 重启信号 */
                }
                VALUE msg = rb_funcall(err, rb_intern("message"), 0);
                char buf[2048];
                snprintf(buf, sizeof buf, "%s: %s",
                         StringValueCStr(cname), StringValueCStr(msg));
                logmsg("[error] section %d: %s", i, buf);
                /* 打印 Ruby backtrace, 定位具体脚本行 */
                {
                    int bst = 0;
                    VALUE bt = rb_eval_string_protect(
                        "$! && $!.backtrace ? $!.backtrace[0, 15].join(' <- ') : '(no backtrace)'",
                        &bst);
                    if (!bst && !NIL_P(bt))
                        logmsg("[backtrace] %s", StringValueCStr(bt));
                }
                MessageBoxA(g_hwnd, buf, "RGSS Error", MB_ICONWARNING);
            }
            return 0;
        }
    }
    return 1;
}

/* ---------- 导出入口 ---------- */

/* 崩溃定位: 记录未处理异常的地址(换算成 DLL 内偏移), 便于与反汇编对照 */
static LONG WINAPI rgss_crash_filter(EXCEPTION_POINTERS *ep)
{
    FILE *f = fopen("rgss_crash.log", "a");
    if (f) {
        DWORD base = (DWORD)(DWORD_PTR)GetModuleHandleA("RGSS202E.dll");
        DWORD ip = (DWORD)ep->ExceptionRecord->ExceptionAddress;
        fprintf(f, "[crash] code=0x%08lX addr=0x%08lX dll_base=0x%08lX offset=0x%lX",
                (unsigned long)ep->ExceptionRecord->ExceptionCode,
                (unsigned long)ip, (unsigned long)base,
                ip >= base ? (unsigned long)(ip - base) : 0ul);
        if (ep->ExceptionRecord->ExceptionCode == 0xC0000005L &&
            ep->ExceptionRecord->NumberParameters >= 2)
            fprintf(f, " op=%s target=0x%p",
                    ep->ExceptionRecord->ExceptionInformation[0] ? "write" : "read",
                    (void *)(DWORD_PTR)ep->ExceptionRecord->ExceptionInformation[1]);
        fputc(10, f);
        fclose(f);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

int __cdecl rgss_game_main(int argc_unused, int b, int c)
{
    SetUnhandledExceptionFilter(rgss_crash_filter);
        int a = argc_unused;
    logmsg("[main] RGSSGameMain(a=0x%X, b=0x%X, c=0x%X) IsWindow(a)=%d",
           (unsigned)a, (unsigned)b, (unsigned)c, IsWindow((HWND)(DWORD_PTR)a));
    if (IsWindow((HWND)(DWORD_PTR)b)) logmsg("  b is also a window");
    if (IsWindow((HWND)(DWORD_PTR)c)) logmsg("  c is also a window");

    /* 阶段 B: 注册图形类（原版 CGameFrame::Run 首步 sub_10009F40） */
    rgss_register_phase_b();
    {
        int st = 0;
        VALUE r1 = rb_eval_string_protect("defined?(Bitmap).to_s", &st);
        VALUE r2 = rb_eval_string_protect("defined?(Graphics).to_s", &st);
        VALUE r3 = rb_eval_string_protect("defined?(Sprite).to_s", &st);
        VALUE r4 = rb_eval_string_protect("defined?(Input).to_s", &st);
        logmsg("[phaseB] Bitmap=%s Graphics=%s Sprite=%s Input=%s",
               NIL_P(r1) ? "nil" : StringValueCStr(r1),
               NIL_P(r2) ? "nil" : StringValueCStr(r2),
               NIL_P(r3) ? "nil" : StringValueCStr(r3),
               NIL_P(r4) ? "nil" : StringValueCStr(r4));
    }

    /* 创建窗口（原版 544x416） */
    /* 实测: Game.exe 传入的是 HMODULE(DLL基址), 不是窗口句柄。
       原版 DLL 内部自行创建窗口, 我们也这样做。 */
    /* Game.exe 可能把宿主窗口作为 GameMain 的参数传入 */
    if (IsWindow((HWND)(DWORD_PTR)a)) {
        g_hwnd = (HWND)(DWORD_PTR)a;
        logmsg("[main] using HOST window=0x%X", (unsigned)g_hwnd);
        SetWindowLongW(g_hwnd, GWL_STYLE,
            GetWindowLongW(g_hwnd, GWL_STYLE) | WS_VISIBLE);
        ShowWindow(g_hwnd, SW_SHOW);
        UpdateWindow(g_hwnd);
    } else if (IsWindow((HWND)(DWORD_PTR)b)) {
        g_hwnd = (HWND)(DWORD_PTR)b;
        logmsg("[main] using HOST window from b=0x%X", (unsigned)g_hwnd);
    } else {
        g_hwnd = create_game_window("RGSS Player", 544, 416);
        logmsg("[main] created own window=0x%X", (unsigned)g_hwnd);
    }
    if (!g_hwnd) return 0;
    rgss_graphics_set_hwnd(g_hwnd, 544, 416);

    read_scripts_path(g_scripts_fname, sizeof g_scripts_fname);

    /* 外层重启循环（F12 / Reset 异常） */
    for (;;) {
        g_reset_requested = 0;
        int rc = load_and_run_scripts(g_scripts_fname);
        if (rc == 2) continue;        /* Reset → 重来 */
        break;
    }

    /* 退出清理（原版字符串） */
    {
        int st = 0;
        rb_eval_string_protect(
            "Graphics._reset;Input.update;Audio.me_stop;Audio.se_stop;"
            "Audio.bgm_stop;Audio.bgs_stop;GC.start;", &st);
    }
    if (g_hwnd) { DestroyWindow(g_hwnd); g_hwnd = NULL; }
    return 0;
}
