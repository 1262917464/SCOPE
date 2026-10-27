/*
 * Win32API.c — RGSS 内置 Win32API 扩展(重建版)
 * 基于 ruby-1.8.1/ext/Win32API, 修正调用实现:
 *   原版 `ApiFunction(param)` 结构体传参模拟 → stdcall callee 弹参与
 *   cdecl caller 清栈叠加, 在现代编译器下栈失衡段错误。
 *   本版按元数显式分派到 WINAPI(stdcall) 原型, 与 Windows API 约定一致。
 */
#include <windows.h>
#include "ruby.h"

#define _T_NUMBER 0
#define _T_POINTER 1
#define _T_INTEGER 2
#define _T_VOID 3

typedef long (WINAPI *APIFN)();

static VALUE
Win32API_initialize(self, dllname, proc, import, export)
    VALUE self, dllname, proc, import, export;
{
    HANDLE hdll;
    FARPROC api;

    SafeStringValue(dllname);
    SafeStringValue(proc);
    hdll = LoadLibraryA(RSTRING(dllname)->ptr);
    if (!hdll) rb_raise(rb_eRuntimeError, "Can't Load Library: %s",
                        RSTRING(dllname)->ptr);
    api = (FARPROC)GetProcAddress(hdll, RSTRING(proc)->ptr);
    if (!api) rb_raise(rb_eRuntimeError, "Can't Find Function: %s",
                       RSTRING(proc)->ptr);

    rb_iv_set(self, "__hdll__", INT2NUM((long)hdll));
    rb_iv_set(self, "__proc__", INT2NUM((long)api));

    {   /* import 类型表: 'V'/'v' 不产生参数(零参), N/L=数值 P=指针 I=整数 */
        VALUE ary = rb_ary_new();
        int i, len;
        const char *s;
        if (NIL_P(import)) {
            /* 保持空表? 原版: nil → 单指针(见下 default) */
            rb_ary_push(ary, INT2FIX(_T_POINTER));
        } else if (TYPE(import) == T_ARRAY) {
            len = RARRAY(import)->len;
            if (16 < len) rb_raise(rb_eRuntimeError, "too many parameters: %d", len);
            for (i = 0; i < len; i++) {
                SafeStringValue(RARRAY(import)->ptr[i]);
                switch (*RSTRING(RARRAY(import)->ptr[i])->ptr) {
                case 'N': case 'n': case 'L': case 'l':
                    rb_ary_push(ary, INT2FIX(_T_NUMBER)); break;
                case 'P': case 'p':
                    rb_ary_push(ary, INT2FIX(_T_POINTER)); break;
                case 'I': case 'i':
                    rb_ary_push(ary, INT2FIX(_T_INTEGER)); break;
                default: break;   /* 'V' 等: 零参数项 */
                }
            }
        } else {
            SafeStringValue(import);
            s = RSTRING(import)->ptr;
            len = RSTRING(import)->len;
            for (i = 0; i < len; i++, s++) {
                switch (*s) {
                case 'N': case 'n': case 'L': case 'l':
                    rb_ary_push(ary, INT2FIX(_T_NUMBER)); break;
                case 'P': case 'p':
                    rb_ary_push(ary, INT2FIX(_T_POINTER)); break;
                case 'I': case 'i':
                    rb_ary_push(ary, INT2FIX(_T_INTEGER)); break;
                default: break;   /* 'V'/'v': 零参数 */
                }
            }
        }
        rb_iv_set(self, "__import__", ary);
    }
    {   /* export 类型 */
        int ex = _T_VOID;
        if (!NIL_P(export) && TYPE(export) == T_STRING) {
            switch (*RSTRING(export)->ptr) {
            case 'V': case 'v': ex = _T_VOID; break;
            case 'N': case 'n': case 'L': case 'l': ex = _T_NUMBER; break;
            case 'P': case 'p': ex = _T_POINTER; break;
            case 'I': case 'i': ex = _T_INTEGER; break;
            }
        }
        rb_iv_set(self, "__export__", INT2FIX(ex));
    }
    return Qnil;
}

static VALUE
Win32API_Call(argc, argv, obj)
    int argc; VALUE *argv; VALUE obj;
{
    VALUE args;
    unsigned long p[16] = {0};
    unsigned long ret = 0;
    int i;
    VALUE obj_proc = rb_iv_get(obj, "__proc__");
    VALUE obj_import = rb_iv_get(obj, "__import__");
    VALUE obj_export = rb_iv_get(obj, "__export__");
    APIFN f = (APIFN)NUM2ULONG(obj_proc);
    int items = rb_scan_args(argc, argv, "0*", &args);
    int nimport = RARRAY(obj_import)->len;

    if (items != nimport)
        rb_raise(rb_eRuntimeError, "Wrong number of parameters: %d for %d",
                 items, nimport);

    for (i = 0; i < nimport && i < 16; i++) {
        VALUE v = rb_ary_entry(args, i);
        switch (FIX2INT(rb_ary_entry(obj_import, i))) {
        case _T_POINTER:
            if (NIL_P(v)) p[i] = 0;
            else if (TYPE(v) == T_STRING) p[i] = (unsigned long)RSTRING(v)->ptr;
            else p[i] = NUM2ULONG(v);
            break;
        default:
            p[i] = NUM2ULONG(v);
            break;
        }
    }

    switch (nimport) {
    case 0:  ret = f(); break;
    case 1:  ret = f(p[0]); break;
    case 2:  ret = f(p[0], p[1]); break;
    case 3:  ret = f(p[0], p[1], p[2]); break;
    case 4:  ret = f(p[0], p[1], p[2], p[3]); break;
    case 5:  ret = f(p[0], p[1], p[2], p[3], p[4]); break;
    case 6:  ret = f(p[0], p[1], p[2], p[3], p[4], p[5]); break;
    case 7:  ret = f(p[0], p[1], p[2], p[3], p[4], p[5], p[6]); break;
    case 8:  ret = f(p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]); break;
    case 9:  ret = f(p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8]); break;
    case 10: ret = f(p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9]); break;
    case 11: ret = f(p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10]); break;
    case 12: ret = f(p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11]); break;
    case 13: ret = f(p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11], p[12]); break;
    case 14: ret = f(p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11], p[12], p[13]); break;
    case 15: ret = f(p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11], p[12], p[13], p[14]); break;
    default: ret = f(p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]); break;
    }

    switch (FIX2INT(obj_export)) {
    case _T_NUMBER: case _T_INTEGER:
        return INT2NUM(ret);
    case _T_POINTER:
        return ret ? rb_str_new2((char *)ret) : Qnil;
    default:
        return INT2NUM(0);
    }
}

void
Init_Win32API()
{
    VALUE cWin32API = rb_define_class("Win32API", rb_cObject);
    rb_define_method(cWin32API, "initialize", Win32API_initialize, 4);
    rb_define_method(cWin32API, "call", Win32API_Call, -1);
    rb_define_alias(cWin32API, "Call", "call");
}
