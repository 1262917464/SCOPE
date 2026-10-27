/*
 * rgss_marshal.c — load_data / save_data（Ruby 1.8 Marshal 封装）
 * 原版: rb_define_global_function("load_data", sub_10009A90, 1)
 *       rb_define_global_function("save_data", sub_10008D90, 2)
 *
 * 注意: Marshal.load 接受 字符串数据 或 IO 对象, 不是文件名。
 *       原版 load_data 语义 = File.open(fname,'rb'){|f| Marshal.load(f)}
 */
#include <windows.h>
#include "ruby.h"

static VALUE marshal_mod(void)
{
    return rb_const_get(rb_cObject, rb_intern("Marshal"));
}

/* load_data(fname) -> 对象 */
VALUE rgss_load_data(VALUE self, VALUE vfname)
{
    (void)self;
    StringValue(vfname);
    VALUE rargs[2] = { vfname, rb_str_new2("rb") };
    VALUE f = rb_funcall2(rb_cFile, rb_intern("open"), 2, rargs);
    VALUE largs[1] = { f };
    VALUE obj = rb_funcall2(marshal_mod(), rb_intern("load"), 1, largs);
    rb_funcall(f, rb_intern("close"), 0);
    return obj;
}

/* save_data(obj, fname[, limit]) */
VALUE rgss_save_data(int argc, VALUE *argv, VALUE self)
{
    (void)self;
    VALUE vobj, vfname, vlimit;
    rb_scan_args(argc, argv, "21", &vobj, &vfname, &vlimit);

    StringValue(vfname);
    VALUE dargs[2] = { vobj, NIL_P(vlimit) ? INT2FIX(-1) : vlimit };
    VALUE s = rb_funcall2(marshal_mod(), rb_intern("dump"), 2, dargs);

    VALUE wargs[2] = { vfname, rb_str_new2("wb") };
    VALUE f = rb_funcall2(rb_cFile, rb_intern("open"), 2, wargs);
    rb_funcall(f, rb_intern("write"), 1, s);
    rb_funcall(f, rb_intern("close"), 0);
    return Qnil;
}
