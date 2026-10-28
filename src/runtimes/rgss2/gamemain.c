/*
 * gamemain.c — RGSSGameMain 主循环实现（重建版）
 *
 * 对齐原版 0x10002890 (RGSSGameMain) + sub_1000AFD0 (主循环) + 0x100014a0 (F12 检查):
 *   - 入口: 读 $TEST 全局 → 旗标(原版 0x1016EFE0, eax==2 即 Qtrue)
 *   - 主循环外: eval("Graphics._reset")(0x1000b0e9, 字符串 0x101047a0)
 *   - 大循环 while(1):
 *       F12 按住 → 泵消息+限帧的迷你帧循环, 窗口关闭则退出 (0x1000b10b)
 *       读 Game.ini → eval load_data('$RGSS_SCRIPTS') + Zlib::Inflate (每轮重读)
 *       逐段: eval("'Section%03d'") → eval("$RGSS_SCRIPTS[i][3]") →
 *             运行标志(0x10173478)=1 → eval(src) → =0
 *       异常 → $!.type.to_s == "Reset" ? 清理链+重启 : 错误弹窗+退出
 *       正常完成 → 清理链 + eval("Graphics._reset;Input.update;Audio...;GC.start") → 退出
 *   - F12 的每帧检查在 graphics_classes.c (原版 0x100014a0, raise 类名 "Reset")
 *   - 每轮清理: ×6 注册表清扫 (rgss_engine_reset_registries) + eval 清理串
 *   - F12 按住: 迷你帧循环 = 泵消息 + 继续合成渲染 + 限帧 (0x10001640/0x10001890)
 *
 * 已知保留差异 (差异清单):
 *   - SetUnhandledExceptionFilter 崩溃日志 (调试辅助, 原版无)
 *   - 清扫后孤儿 Ruby 对象的 disposed? 语义 (原版置引擎侧 disposed, 重建由 GC 释放)
 *   - Game.rgssad 归档读取未实现 (差异清单 S19), 仅支持散装 Data/
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "ruby.h"
#include "debug.h"

void rgss_register_phase_a(void);
void rgss_register_phase_b(void);
void rgss_graphics_set_hwnd(HWND h, int w, int hh);
void rgss_engine_reset_registries(void);   /* graphics_classes.c: ×6 清扫等价 */
void rgss_render_clear(unsigned int argb); /* render.c */
void rgss_compose_scene(void);
void rgss_render_present(void);
int  rgss_current_frame_rate(void);

static HWND g_hwnd;
static int  g_test_flag;        /* 原版 0x1016EFE0: RGSSGameMain 读 $TEST */
static int  g_scripts_loading;  /* 原版 0x1015B0C0: 脚本装载中 */
static int  g_script_running;   /* 原版 0x10173478: 脚本段执行中 */

/* ---------- F12 每帧检查 (原版 0x100014a0) ---------- */
/* graphics_classes.c 的 Graphics.update/fadeout/fadein/transition 每帧调用。
 * F12 按下 → raise 类名 "Reset" 的异常, 从 eval 冒出到主循环实现重启。 */
void rgss_f12_check(void)
{
    if ((SHORT)GetAsyncKeyState(VK_F12) < 0) {
        VALUE cls = rb_define_class("Reset", rb_eException); /* 已定义则返回原类 */
        rb_raise(cls, "Reset");
    }
}

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

/* ---------- Game.ini 读取 (原版每轮循环重读, sub_10008C70) ---------- */
static int read_scripts_path(char *out, size_t outsz)
{
    char ini[MAX_PATH];
    out[0] = 0;
    GetModuleFileNameA(NULL, ini, MAX_PATH);
    char *p = strrchr(ini, '\\');
    if (!p) return 0;
    strcpy(p + 1, "Game.ini");
    GetPrivateProfileStringA("Game", "Scripts", "Data\\Scripts.rvdata",
                             out, (DWORD)outsz, ini);
    /* 原版前置检查 sub_1000ACC0: 归档或散装文件可读; 重建只支持散装 (S19) */
    if (!out[0]) return 0;
    DWORD attr = GetFileAttributesA(out);
    return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
}

/* ---------- 每轮清理 (原版 0x1000b3c8-0x1000b3f6) ----------
 * 原版先做 6 个 C 侧子系统清理 (sub_100048F0/10010EA0/1000E080/1000F970/
 * 1000BF70/10012100, 归属未定未还原, 见差异清单 B49), 再 eval 清理串。成功与
 * Reset 两路都经过这里。 */
static void rgss_cleanup_pass(void)
{
    /* 原版 ×6: 引擎对象注册表 dispose 清扫 (B49 已还原部分) */
    rgss_engine_reset_registries();
    int st = 0;
    rb_eval_string_protect(
        "Graphics._reset;Input.update;Audio.me_stop;Audio.se_stop;"
        "Audio.bgm_stop;Audio.bgs_stop;GC.start;", &st);
}

/* ---------- 错误弹窗 (原版 sub_1000A080) ---------- */
static void show_script_error(void)
{
    VALUE err = rb_gv_get("$!");
    char buf[2048];
    if (NIL_P(err)) {
        lstrcpynA(buf, "Unknown error ($! is nil)", (int)sizeof buf);
    } else {
        int st = 0;
        VALUE bt = rb_eval_string_protect(
            "$!.backtrace ? $!.backtrace[0,8].join(\"\\n\") : ''", &st);
        VALUE msg = rb_funcall(err, rb_intern("message"), 0);
        _snprintf(buf, sizeof buf - 1, "%s: %s%s%.700s",
                  rb_obj_classname(err), StringValueCStr(msg),
                  (!st && !NIL_P(bt) && RSTRING(bt)->len) ? "\n" : "",
                  (!st && !NIL_P(bt)) ? StringValueCStr(bt) : "");
        buf[sizeof buf - 1] = 0;
    }
    MessageBoxA(g_hwnd, buf, "RGSS Error", MB_ICONWARNING);
}

/* ---------- 主循环一次迭代 (原版 sub_1000AFD0 的 while(1) 体) ----------
 * 返回 0=正常结束, 1=Reset 重启, -1=错误(已弹窗, 退出) */
static int run_scripts_pass(void)
{
    char code[4096];
    char fname[260];
    int state = 0;

    /* 原版 0x1000b10b: F12 按住 → 迷你帧循环 (0x10001640 限帧 + 0x10001890
     * 泵消息+继续渲染), 窗口关闭 → 退出 */
    while ((SHORT)GetAsyncKeyState(VK_F12) < 0) {
        MSG msg;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (!g_hwnd || !IsWindow(g_hwnd)) return 0;
        rgss_render_clear(0xFF000000u);
        rgss_compose_scene();
        rgss_render_present();
        Sleep(1000 / (rgss_current_frame_rate() > 0 ? rgss_current_frame_rate() : 60));
    }

    /* 原版 sub_10008C70: 每轮重读 Game.ini */
    if (!read_scripts_path(fname, sizeof fname)) {
        /* 原版: LoadStringW 资源 id 8 错误模板 → 弹窗 (0x1000b03d-0x1000b0a4) */
        MessageBoxA(g_hwnd, "Failed to load Scripts.\n(Game.ini [Game] Scripts)",
                    "RGSS Error", MB_ICONERROR);
        return -1;
    }

    /* 读入 $RGSS_SCRIPTS (原版 aRgssScriptsLoa eval 串, 0x1000b18a) */
    snprintf(code, sizeof code,
        "$RGSS_SCRIPTS = load_data('%s');"
        "$RGSS_SCRIPTS.each { |s| s[3,0] = Zlib::Inflate.inflate(s[2]) };"
        "$RGSS_SCRIPTS.size", fname);
    g_scripts_loading = 1;                       /* 原版 0x1015B0C0 */
    VALUE n = rb_eval_string_protect(code, &state);
    g_scripts_loading = 0;
    if (state) goto exception;

    {
        int count = NUM2INT(n);
        for (int i = 0; i < count; i++) {
            state = 0;
            /* eval("'Section%03d'") — 包装段名, 错误栈显示用 (0x1000b252) */
            snprintf(code, sizeof code, "'Section%03d'", i);
            rb_eval_string_protect(code, &state);
            if (state) goto exception;

            /* eval("$RGSS_SCRIPTS[%d][3]") (0x1000b292) */
            snprintf(code, sizeof code, "$RGSS_SCRIPTS[%d][3]", i);
            VALUE src = rb_eval_string_protect(code, &state);
            if (state) goto exception;
            if (NIL_P(src) || TYPE(src) != T_STRING) continue;

            /* 运行标志 = 1 → eval(src) → = 0 (0x1000b2c1-0x1000b2f1) */
#if RGSS_DEBUG_TOOLS
            /* 调试钩: RGSS_TEST_EVAL → 每段前 eval (须幂等; 游戏入口主循环
             * 所在段会永久阻塞, 之后注入不可达) */
            {
                const char *test_eval = getenv("RGSS_TEST_EVAL");
                if (test_eval && test_eval[0]) {
                    state = 0;
                    g_script_running = 1;
                    rb_eval_string_protect(test_eval, &state);
                    g_script_running = 0;
                    if (state) goto exception;
                }
            }
#endif
            g_script_running = 1;                /* 原版 0x10173478 */
            rb_eval_string_protect(RSTRING(src)->ptr, &state);
            g_script_running = 0;
            if (state) goto exception;
        }
    }

    /* 正常完成: 清理链 → 结束 (原版 v18=false → return false) */
    rgss_cleanup_pass();
    return 0;

exception:
    /* 原版 LABEL_29: $!.type.to_s == "Reset" → 清理链+重启; 否则弹窗+退出。
     * 补充分支: SystemExit (Graphics.update 窗口关闭路径 / 脚本显式 exit)
     * → 清理链 + 干净退出, 不弹窗。 */
    {
        VALUE err = rb_gv_get("$!");
        if (!NIL_P(err)) {
            const char *cname = rb_obj_classname(err);
            {   /* 诊断: 异常落盘 (临时, 走标准日志出口) */
                FILE *f = RGSS_LOG_FOPEN("rgss_error.log");
                if (f) {
                    VALUE msg = rb_funcall(err, rb_intern("message"), 0);
                    fprintf(f, "exc=%s msg=%s\n", cname ? cname : "?",
                            NIL_P(msg) ? "?" : RSTRING(msg)->ptr);
                    fclose(f);
                }
            }
            if (cname != NULL && strcmp(cname, "Reset") == 0) {
                rgss_cleanup_pass();
                return 1;
            }
            if (cname != NULL && strcmp(cname, "SystemExit") == 0) {
                rgss_cleanup_pass();
                return 0;
            }
        }
        show_script_error();
        return -1;
    }
}

/* ---------- 崩溃转储 (调试辅助, 原版无 — RGSS_DEBUG_TOOLS=0 时整体编译去除) ----------
 * B44: 捕获未处理 SEH 异常, 输出寄存器/模块表/栈顶到 rgss_crash.log,
 * 便于把崩溃地址换算成 DLL 偏移与反汇编对照。 */
#if RGSS_DEBUG_TOOLS
#include <tlhelp32.h>

void rgss_debug_cleanup(void)
{
    for (int i = 0; rgss_debug_log_names[i]; i++)
        DeleteFileA(rgss_debug_log_names[i]);
}

static void dump_modules(FILE *f, DWORD crash_ip)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE) return;
    MODULEENTRY32W me;   /* 显式 W 版: szModule 恒为宽字符, 与 UNICODE 宏无关 */
    me.dwSize = sizeof me;
    if (Module32FirstW(snap, &me)) {
        fprintf(f, "[modules]\n");
        do {
            DWORD lo = (DWORD)(DWORD_PTR)me.modBaseAddr;
            fprintf(f, "  %08lX-%08lX %ls%s\n", lo, lo + me.modBaseSize - 1,
                    me.szModule,
                    (crash_ip >= lo && crash_ip < lo + me.modBaseSize) ? "  <-- crash" : "");
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
}

static LONG WINAPI rgss_crash_filter(EXCEPTION_POINTERS *ep)
{
    FILE *f = RGSS_LOG_FOPEN("rgss_crash.log");
    if (!f) return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT *c = ep->ContextRecord;
    DWORD ip = (DWORD)ep->ExceptionRecord->ExceptionAddress;
    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(f, "[crash] %04d-%02d-%02d %02d:%02d:%02d code=0x%08lX addr=0x%08lX\n",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
            (unsigned long)ep->ExceptionRecord->ExceptionCode, (unsigned long)ip);
    {
        DWORD base = (DWORD)(DWORD_PTR)GetModuleHandleA("RGSS202E.dll");
        fprintf(f, "  dll_base=0x%08lX offset=0x%lX\n", base,
                ip >= base ? (unsigned long)(ip - base) : 0ul);
    }
    if (ep->ExceptionRecord->ExceptionCode == 0xC0000005L &&
        ep->ExceptionRecord->NumberParameters >= 2)
        fprintf(f, "  access=%s target=0x%p\n",
                ep->ExceptionRecord->ExceptionInformation[0] ? "write" : "read",
                (void *)(DWORD_PTR)ep->ExceptionRecord->ExceptionInformation[1]);
    fprintf(f, "  EIP=%08lX ESP=%08lX EBP=%08lX EFL=%08lX\n",
            (unsigned long)c->Eip, (unsigned long)c->Esp,
            (unsigned long)c->Ebp, (unsigned long)c->EFlags);
    fprintf(f, "  EAX=%08lX EBX=%08lX ECX=%08lX EDX=%08lX ESI=%08lX EDI=%08lX\n",
            (unsigned long)c->Eax, (unsigned long)c->Ebx, (unsigned long)c->Ecx,
            (unsigned long)c->Edx, (unsigned long)c->Esi, (unsigned long)c->Edi);
    {   /* 栈顶 16 个双字 (地址=值) */
        DWORD *sp = (DWORD *)(DWORD_PTR)c->Esp;
        fprintf(f, "  stack:");
        for (int i = 0; i < 16; i++) {
            MEMORY_BASIC_INFORMATION mbi;
            if (!VirtualQuery(sp + i, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT) break;
            fprintf(f, " %08lX", sp[i]);
        }
        fprintf(f, "\n");
    }
    dump_modules(f, ip);
    fclose(f);
    return EXCEPTION_CONTINUE_SEARCH;
}
#else
/* rgss_debug_cleanup 由 debug.h 提供 (空实现) */
static LONG WINAPI rgss_crash_filter(EXCEPTION_POINTERS *ep)
{
    (void)ep;
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

/* ---------- 导出入口 ---------- */
int __cdecl rgss_game_main(int a, int b, int c)
{
    /* 过滤器必须最先安装: 自测崩溃/任何早期崩溃都要能被捕获 */
    SetUnhandledExceptionFilter(rgss_crash_filter);

#if RGSS_DEBUG_TOOLS
    /* B44: 启动即清理上一轮日志 (运行期不再累积) */
    rgss_debug_cleanup();
    /* 崩溃转储通路自测: 仅当 RGSS_DEBUG_CRASH_TEST 为非 0 值时自毁
     * (设 "0" 也不触发, 否则无法显式关闭) */
    {
        char v[8];
        if (GetEnvironmentVariableA("RGSS_DEBUG_CRASH_TEST", v, sizeof v) > 0 &&
            v[0] != '0' && v[0] != '\0') {
            volatile int *p = (volatile int *)0;
            *p = 1;   /* 故意崩溃 → rgss_crash.log */
        }
    }
#endif

    /* 原版 0x10002890: 读 $TEST 全局 → 旗标 (Qtrue=2) */
    g_test_flag = (rb_gv_get("$TEST") == Qtrue);

    /* 阶段 B: 注册图形类 (原版 CGameFrame::Run 首步 sub_10009F40) */
    rgss_register_phase_b();

    /* 窗口: 优先宿主窗口 (Game.exe), 否则自建 */
    if (IsWindow((HWND)(DWORD_PTR)a)) {
        g_hwnd = (HWND)(DWORD_PTR)a;
        SetWindowLongW(g_hwnd, GWL_STYLE,
            GetWindowLongW(g_hwnd, GWL_STYLE) | WS_VISIBLE);
        ShowWindow(g_hwnd, SW_SHOW);
        UpdateWindow(g_hwnd);
    } else if (IsWindow((HWND)(DWORD_PTR)b)) {
        g_hwnd = (HWND)(DWORD_PTR)b;
    } else {
        g_hwnd = create_game_window("RGSS Player", 544, 416);
    }
    if (!g_hwnd) return 0;
    rgss_graphics_set_hwnd(g_hwnd, 544, 416);

    /* 原版 0x1000b0e9: eval("Graphics._reset") — 大循环前一次 */
    {
        int st = 0;
        rb_eval_string_protect("Graphics._reset", &st);
    }

    /* 大循环 (原版 while(true) @0x1000b0f6): Reset → 重启, 其余退出 */
    for (;;) {
        int rc = run_scripts_pass();
        if (rc == 1) continue;   /* Reset 重启 */
        break;                   /* 0 正常 / -1 错误 */
    }

    if (g_hwnd) { DestroyWindow(g_hwnd); g_hwnd = NULL; }
    return 0;
}
