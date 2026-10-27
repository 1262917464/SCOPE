/*
 * ruby_test.c — M3 验收件: Ruby 1.8.1 静态库嵌入式 DLL
 * 导出:
 *   RubyInit()                 初始化解释器 (对应原版 RGSSInitialize2 的 Ruby 部分)
 *   int RubyEval(const char*)  eval 表达式, 返回 Fixnum 结果 (RGSSEval 等价)
 *   const char* RubyEvalStr(const char*)  eval 返回字符串 (p/inspect 风格)
 * 对照原版: sub_100330A0 = eval 原语 (rb_eval_string_protect 风格)
 */
#include <windows.h>
#include "ruby.h"

static char g_buf[1024];
static int g_state;

__declspec(dllexport) void RubyInit(void)
{
    ruby_init();
    ruby_script("rgss-rebuild-m3");
}

__declspec(dllexport) int __cdecl RubyEval(const char *code)
{
    VALUE v = rb_eval_string_protect(code, &g_state);
    if (g_state) return -1;
    if (FIXNUM_P(v)) return FIX2INT(v);
    return -2;
}

__declspec(dllexport) const char * __cdecl RubyEvalStr(const char *code)
{
    VALUE v = rb_eval_string_protect(code, &g_state);
    if (g_state) {
        /* 异常时返回错误类名, 模拟原版 RGSSErrorMessage 行为 */
        VALUE e = rb_gv_get("$!");
        g_buf[0] = 0;
        if (!NIL_P(e)) {
            VALUE cls = rb_funcall(e, rb_intern("class"), 0);
            VALUE s = rb_funcall(e, rb_intern("message"), 0);
            snprintf(g_buf, sizeof g_buf, "%s: %s",
                     rb_class2name(rb_obj_class(e)), RSTRING(s)->ptr);
        }
        return g_buf;
    }
    VALUE s = rb_funcall(v, rb_intern("inspect"), 0);
    snprintf(g_buf, sizeof g_buf, "%s", RSTRING(s)->ptr);
    return g_buf;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r)
{
    (void)h; (void)r;
    return reason == DLL_PROCESS_ATTACH ? TRUE : TRUE;
}

/* Win32API 扩展(原版内置) */
extern void Init_Win32API(void);
__declspec(dllexport) void RubyInitWin32API(void) { Init_Win32API(); }
