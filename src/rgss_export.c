/*
 * rgss_export.c — RGSS202E.dll 重建版: 69 个导出桩
 * 目标: drop-in 替换, 导出表与原版 1:1
 * 阶段: 骨架 (M5-e) — 结构就位, 逐步填充实现
 */
#include <windows.h>
#include <stdio.h>
#include "ruby.h"

/* ---------- 全局状态 (地址对照原版) ---------- */
static int   g_running;          /* 原版 0x10173478: 脚本运行标志 */
static void *g_game;             /* 原版 0x1016EFE4: CGameFrame 单例 */
static void *g_audio;            /* 原版 0x1016EFE8: CSxAudio 单例 */
static HWND g_rgss_hwnd;   /* 原版 0x1017347C: 主窗口 */
static int   g_test_mode;        /* 原版 0x1016EFE0: $TEST 标志 */
static char  g_err_type[1024];   /* 原版 0x1016F7F8 */
static char  g_err_msg[1024];    /* 原版 0x1016FFF8 */

/* ---------- 求值原语 (原版 sub_100330A0) ---------- */
static VALUE eval_protect(const char *code, int *state)
{
    VALUE v;
    int st = 0;
    v = rb_eval_string_protect(code, &st);
    if (state) *state = st;
    return v;
}

/* ---------- 1. 生命周期 ---------- */
/* 由 rgss_classes.c / rgss_gamemain.c 提供 */
void rgss_register_phase_a(void);
int  __cdecl rgss_game_main(int a, int b, int c);

__declspec(dllexport) int __cdecl RGSSInitialize2(int hwnd)
{
    int st = 0;
    ruby_init();
    ruby_script("rgss-rebuild");
    g_rgss_hwnd = (HWND)(DWORD_PTR)hwnd;
    { FILE *df = fopen("rgss_init.log","a");
      if (df) { fprintf(df, "[init] param=%d(0x%X) stored=%p\n", hwnd, hwnd, (void*)g_rgss_hwnd); fclose(df); } }

    /* 阶段 A: 数据类注册 + Ruby 环境准备（原版 sub_10009E60 顺序） */
    rb_eval_string_protect("$KCODE = 'U'", &st);
    rb_eval_string_protect("Dir.glob('.')", &st);
    rgss_register_phase_a();
    return hwnd;
}

HWND rgss_get_host_hwnd(void) { return g_rgss_hwnd; }

__declspec(dllexport) void __cdecl RGSSFinalize(void)
{
    g_running = 0;
}

__declspec(dllexport) int __cdecl RGSSGameMain(int a, int b, int c)
{
    return rgss_game_main(a, b, c);
}

__declspec(dllexport) int __cdecl RGSSEval(const char *code)
{
    int st = 0;
    eval_protect(code, &st);
    return st;
}

/* ---------- 2. 数据桥 ---------- */
__declspec(dllexport) int __cdecl RGSSGetBool(const char *code)
{
    int st = 0;
    VALUE v = eval_protect(code, &st);
    return !st && v == Qfalse;
}

__declspec(dllexport) int __cdecl RGSSGetInt(const char *code)
{
    int st = 0;
    VALUE v = eval_protect(code, &st);
    if (st) return 0;
    if (FIXNUM_P(v)) return FIX2INT(v);
    if (rb_type(v) == T_FLOAT) return (int)RFLOAT(v)->value;
    return 0;
}

__declspec(dllexport) double __cdecl RGSSGetDouble(const char *code)
{
    int st = 0;
    VALUE v = eval_protect(code, &st);
    if (st) return 0.0;
    if (FIXNUM_P(v)) return (double)FIX2INT(v);
    if (rb_type(v) == T_FLOAT) return RFLOAT(v)->value;
    return 0.0;
}

/* Table: 原版读 T_DATA 内嵌 4 个 int (xsize/ysize/zsize/...)
   本版返回 4 个 int 的输出缓冲, 布局待 M5-e 校准 */
__declspec(dllexport) void __cdecl RGSSGetTable(int *out, const char *code)
{
    int st = 0;
    VALUE v = eval_protect(code, &st);
    if (out) { out[0] = out[1] = out[2] = out[3] = 0; }
    if (st || NIL_P(v)) return;
    /* TODO: 取 Table 的 xsize/ysize/zsize */
    (void)v;
}

/* ---------- 3. 字符串桥 ---------- */
static VALUE get_str_value(const char *code, int *st)
{
    return eval_protect(code, st);
}

__declspec(dllexport) const char * __cdecl RGSSGetStringUTF8(const char *code)
{
    static char buf[4096];
    int st = 0;
    VALUE v = get_str_value(code, &st);
    if (st || !TYPE(v) == T_STRING) { buf[0] = 0; return buf; }
    lstrcpynA(buf, RSTRING(v)->ptr, sizeof buf);
    return buf;
}

__declspec(dllexport) const char * __cdecl RGSSGetStringACP(const char *code)
{
    static char buf[4096];
    int st = 0;
    VALUE v = get_str_value(code, &st);
    buf[0] = 0;
    if (st || !TYPE(v) == T_STRING) return buf;
    MultiByteToWideChar(CP_UTF8, 0, RSTRING(v)->ptr, -1, (LPWSTR)buf, 0);
    return buf;
}

__declspec(dllexport) const wchar_t * __cdecl RGSSGetStringUTF16(const char *code)
{
    static wchar_t buf[2048];
    int st = 0;
    VALUE v = get_str_value(code, &st);
    buf[0] = 0;
    if (st || !TYPE(v) == T_STRING) return buf;
    MultiByteToWideChar(CP_UTF8, 0, RSTRING(v)->ptr, -1, buf, 2048);
    return buf;
}

__declspec(dllexport) int __cdecl RGSSSetString(const char *var, const char *val)
{
    char code[2048];
    wsprintfA(code, "%s = '%s'", var, val);
    int st = 0;
    eval_protect(code, &st);
    return !st;
}

__declspec(dllexport) int __cdecl RGSSSetStringUTF8(const char *var, const char *val)
{
    return RGSSSetString(var, val);
}

__declspec(dllexport) int __cdecl RGSSSetStringACP(const char *var, const char *val)
{
    char u8[2048];
    WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR)val, -1, u8, sizeof u8, NULL, NULL);
    return RGSSSetString(var, u8);
}

__declspec(dllexport) int __cdecl RGSSSetStringUTF16(const char *var, const wchar_t *val)
{
    char u8[2048];
    WideCharToMultiByte(CP_UTF8, 0, val, -1, u8, sizeof u8, NULL, NULL);
    return RGSSSetString(var, u8);
}

/* ---------- 4. 错误信息 ---------- */
__declspec(dllexport) char * __cdecl RGSSErrorType(void) { return g_err_type; }
__declspec(dllexport) char * __cdecl RGSSErrorMessage(void) { return g_err_msg; }

/* ---------- 5. RTP / 字体 ---------- */
/* Game.exe 实测: RGSSSetupRTP() 无参数 */
__declspec(dllexport) int __cdecl RGSSSetupRTP(void) { return 1; }

__declspec(dllexport) int __cdecl RGSSAddRTPPath(const wchar_t *p) { (void)p; return 0; }
__declspec(dllexport) int __cdecl RGSSClearRTPPath(void) { return 0; }
__declspec(dllexport) int __cdecl RGSSGetRTPPath(const void *p) { (void)p; return 0; }

__declspec(dllexport) wchar_t * __cdecl RGSSGetPathWithRTP(const wchar_t *name)
{
    static wchar_t buf[512];
    if (name) lstrcpynW(buf, name, 512);
    else buf[0] = 0;
    return buf;
}

/* RGSSSetupFonts — 注册游戏自带字体
 * 原版 sub_10009110 实测逻辑:
 *   1. 取宽字符串路径 (实测函数体读第 2 参)
 *   2. 拼 <path>\Fonts\*  (模式串 0x101043AC = L"\\Fonts\\*")
 *   3. FindFirstFileW 枚举
 *   4. 每个文件: <path>\Fonts\<cFileName> → AddFontResourceExW(p, FR_PRIVATE, 0)
 *      (实测 push 0x10 = FR_PRIVATE, 私有加载不污染系统)
 *   5. FindNextFileW 循环, FindClose 收尾
 */
__declspec(dllexport) int __cdecl RGSSSetupFonts(void *arg1, void *arg2, int arg3)
{
    (void)arg1; (void)arg2; (void)arg3;
    wchar_t root[MAX_PATH];
    wchar_t pattern[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    int count = 0;

    /* 路径来源: Game.exe 所在目录。
     * 注意: 不能把 arg2 当宽字符串解引用 —— Game.exe 在 RGSSGameMain
     * 之前就调用本函数, 此时异常过滤器尚未安装, 乱解引用直接崩进程 */
    {
        char exe[MAX_PATH];
        wchar_t wexe[MAX_PATH];
        GetModuleFileNameA(NULL, exe, MAX_PATH);
        char *p = strrchr(exe, '\\');
        if (p) *p = 0;
        MultiByteToWideChar(CP_ACP, 0, exe, -1, wexe, MAX_PATH);
        lstrcpynW(root, wexe, MAX_PATH);
    }

    lstrcpynW(pattern, root, MAX_PATH);
    lstrcatW(pattern, L"\\Fonts\\*");
    h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 1;   /* 无字体目录不算错误 */

    do {
        wchar_t full[MAX_PATH];
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        lstrcpynW(full, root, MAX_PATH);
        lstrcatW(full, L"\\Fonts\\");
        lstrcatW(full, fd.cFileName);
        if (AddFontResourceExW(full, FR_PRIVATE, 0)) count++;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return 1;
}

/* ---------- 6. 音频 / GC ---------- */
__declspec(dllexport) int __cdecl RGSSAudioInitialize(void) { return 0; }
__declspec(dllexport) int __cdecl RGSSAudioFinalize(void) { return 0; }
__declspec(dllexport) int __cdecl RGSSGC(void) { rb_gc_start(); return 0; }

/* ---------- 7. Ruby 正则引擎透传 (36 个) ----------
   直接复用 Ruby 1.8.1 静态库内的同名符号。
   注: 原版这些是真实导出, 这里用转发实现。 */
/* TODO(M5-e): 逐个声明 extern 并转发; 目前先保证导出表结构完整 */

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r)
{
    (void)h; (void)r;
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(h);
    return TRUE;
}
