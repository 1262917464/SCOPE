/*
 * rgss_data_classes.c — Color / Tone / Rect / Table / Font 实现
 * 行为依据: M5-c 运行时反射实测
 *   Color: 浮点存储, alpha 默认 255; 方法 == === _dump alpha alpha= blue blue= eql?
 *          green green= red red= set to_s + 单例 _load
 *   Tone : 同 Color 骨架但 gray/gray= 替代 alpha
 *   Rect : 整数存储; == === _dump empty eql? height height= set to_s width width=
 *          x x= y y= + _load; set arity=4
 *   Table: [] []= _dump resize xsize ysize zsize + _load
 */
#include <windows.h>
#include <stdio.h>
#include "ruby.h"

static VALUE cColor, cTone, cRect, cTable, cFont;

/* ================= Color ================= */
typedef struct { double r, g, b, a; } ColorVal;

static void color_free(void *p) { free(p); }
static VALUE color_alloc(VALUE k) { return Data_Wrap_Struct(k, 0, color_free, calloc(1, sizeof(ColorVal))); }
#define GET_COLOR(v) ((ColorVal *)DATA_PTR(v))

static VALUE color_initialize(int argc, VALUE *argv, VALUE self)
{
    VALUE vr, vg, vb, va;
    rb_scan_args(argc, argv, "31", &vr, &vg, &vb, &va);
    ColorVal *c = GET_COLOR(self);
    c->r = NUM2DBL(vr); c->g = NUM2DBL(vg); c->b = NUM2DBL(vb);
    c->a = NIL_P(va) ? 255.0 : NUM2DBL(va);
    return self;
}

static VALUE color_set(int argc, VALUE *argv, VALUE self)
{
    return color_initialize(argc, argv, self);
}

static VALUE color_to_s(VALUE self)
{
    ColorVal *c = GET_COLOR(self);
    char buf[128];
    sprintf(buf, "(%f, %f, %f, %f)", c->r, c->g, c->b, c->a);
    return rb_str_new2(buf);
}

#define COLOR_GETTER(name, field) \
    static VALUE color_##name(VALUE self) { return rb_float_new(GET_COLOR(self)->field); }
#define COLOR_SETTER(name, field) \
    static VALUE color_##name##_set(VALUE self, VALUE v) { GET_COLOR(self)->field = NUM2DBL(v); return v; }

COLOR_GETTER(red, r)     COLOR_SETTER(red, r)
COLOR_GETTER(green, g)   COLOR_SETTER(green, g)
COLOR_GETTER(blue, b)    COLOR_SETTER(blue, b)
COLOR_GETTER(alpha, a)   COLOR_SETTER(alpha, a)

static VALUE color_eql(VALUE self, VALUE other)
{
    if (!rb_obj_is_kind_of(other, cColor)) return Qfalse;
    ColorVal *a = GET_COLOR(self), *b = GET_COLOR(other);
    return (a->r == b->r && a->g == b->g && a->b == b->b && a->a == b->a) ? Qtrue : Qfalse;
}

static VALUE color_dump(VALUE self, VALUE depth)
{
    ColorVal *c = GET_COLOR(self);
    char buf[128];
    (void)depth;
    sprintf(buf, "%f,%f,%f,%f", c->r, c->g, c->b, c->a);
    return rb_str_new2(buf);
}

static VALUE color_load(VALUE klass, VALUE str)
{
    ColorVal tmp = {0, 0, 0, 255};
    StringValue(str);
    sscanf(RSTRING(str)->ptr, "%lf,%lf,%lf,%lf", &tmp.r, &tmp.g, &tmp.b, &tmp.a);
    VALUE obj = color_alloc(klass);
    *GET_COLOR(obj) = tmp;
    return obj;
}

/* ================= Tone ================= */
typedef struct { double r, g, b, gray; } ToneVal;

static void tone_free(void *p) { free(p); }
static VALUE tone_alloc(VALUE k) { return Data_Wrap_Struct(k, 0, tone_free, calloc(1, sizeof(ToneVal))); }
#define GET_TONE(v) ((ToneVal *)DATA_PTR(v))

static VALUE tone_initialize(int argc, VALUE *argv, VALUE self)
{
    VALUE vr, vg, vb, vgr;
    rb_scan_args(argc, argv, "31", &vr, &vg, &vb, &vgr);
    ToneVal *t = GET_TONE(self);
    t->r = NUM2DBL(vr); t->g = NUM2DBL(vg); t->b = NUM2DBL(vb);
    t->gray = NIL_P(vgr) ? 0.0 : NUM2DBL(vgr);
    return self;
}

static VALUE tone_set(int argc, VALUE *argv, VALUE self) { return tone_initialize(argc, argv, self); }

static VALUE tone_to_s(VALUE self)
{
    ToneVal *t = GET_TONE(self);
    char buf[128];
    sprintf(buf, "(%f, %f, %f, %f)", t->r, t->g, t->b, t->gray);
    return rb_str_new2(buf);
}

#define TONE_GETTER(name, field) \
    static VALUE tone_##name(VALUE self) { return rb_float_new(GET_TONE(self)->field); }
#define TONE_SETTER(name, field) \
    static VALUE tone_##name##_set(VALUE self, VALUE v) { GET_TONE(self)->field = NUM2DBL(v); return v; }

TONE_GETTER(red, r)   TONE_SETTER(red, r)
TONE_GETTER(green, g) TONE_SETTER(green, g)
TONE_GETTER(blue, b)  TONE_SETTER(blue, b)
TONE_GETTER(gray, gray) TONE_SETTER(gray, gray)

static VALUE tone_eql(VALUE self, VALUE other)
{
    if (!rb_obj_is_kind_of(other, cTone)) return Qfalse;
    ToneVal *a = GET_TONE(self), *b = GET_TONE(other);
    return (a->r == b->r && a->g == b->g && a->b == b->b && a->gray == b->gray) ? Qtrue : Qfalse;
}

static VALUE tone_dump(VALUE self, VALUE depth)
{
    ToneVal *t = GET_TONE(self);
    char buf[128];
    (void)depth;
    sprintf(buf, "%f,%f,%f,%f", t->r, t->g, t->b, t->gray);
    return rb_str_new2(buf);
}

static VALUE tone_load(VALUE klass, VALUE str)
{
    ToneVal tmp = {0, 0, 0, 0};
    StringValue(str);
    sscanf(RSTRING(str)->ptr, "%lf,%lf,%lf,%lf", &tmp.r, &tmp.g, &tmp.b, &tmp.gray);
    VALUE obj = tone_alloc(klass);
    *GET_TONE(obj) = tmp;
    return obj;
}

/* ================= Rect ================= */
typedef struct { int x, y, w, h; } RectVal;

static void rect_free(void *p) { free(p); }
static VALUE rect_alloc(VALUE k) { return Data_Wrap_Struct(k, 0, rect_free, calloc(1, sizeof(RectVal))); }
#define GET_RECT(v) ((RectVal *)DATA_PTR(v))

static VALUE rect_initialize(int argc, VALUE *argv, VALUE self)
{
    VALUE vx, vy, vw, vh;
    rb_scan_args(argc, argv, "22", &vx, &vy, &vw, &vh);
    RectVal *r = GET_RECT(self);
    r->x = NIL_P(vx) ? 0 : NUM2INT(vx);
    r->y = NIL_P(vy) ? 0 : NUM2INT(vy);
    r->w = NIL_P(vw) ? 0 : NUM2INT(vw);
    r->h = NIL_P(vh) ? 0 : NUM2INT(vh);
    return self;
}

static VALUE rect_set(VALUE self, VALUE vx, VALUE vy, VALUE vw, VALUE vh)
{
    RectVal *r = GET_RECT(self);
    r->x = NUM2INT(vx); r->y = NUM2INT(vy);
    r->w = NUM2INT(vw); r->h = NUM2INT(vh);
    return self;
}

static VALUE rect_empty(VALUE self)
{
    RectVal *r = GET_RECT(self);
    r->x = r->y = r->w = r->h = 0;
    return self;
}

static VALUE rect_to_s(VALUE self)
{
    RectVal *r = GET_RECT(self);
    char buf[96];
    sprintf(buf, "(%d, %d, %d, %d)", r->x, r->y, r->w, r->h);
    return rb_str_new2(buf);
}

#define RECT_GETTER(name, field) \
    static VALUE rect_##name(VALUE self) { return INT2FIX(GET_RECT(self)->field); }
#define RECT_SETTER(name, field) \
    static VALUE rect_##name##_set(VALUE self, VALUE v) { GET_RECT(self)->field = NUM2INT(v); return v; }

RECT_GETTER(x, x)   RECT_SETTER(x, x)
RECT_GETTER(y, y)   RECT_SETTER(y, y)
RECT_GETTER(width, w)  RECT_SETTER(width, w)
RECT_GETTER(height, h) RECT_SETTER(height, h)

static VALUE rect_eql(VALUE self, VALUE other)
{
    if (!rb_obj_is_kind_of(other, cRect)) return Qfalse;
    RectVal *a = GET_RECT(self), *b = GET_RECT(other);
    return (a->x == b->x && a->y == b->y && a->w == b->w && a->h == b->h) ? Qtrue : Qfalse;
}

static VALUE rect_dump(VALUE self, VALUE depth)
{
    RectVal *r = GET_RECT(self);
    char buf[96];
    (void)depth;
    sprintf(buf, "%d,%d,%d,%d", r->x, r->y, r->w, r->h);
    return rb_str_new2(buf);
}

static VALUE rect_load(VALUE klass, VALUE str)
{
    RectVal tmp = {0, 0, 0, 0};
    StringValue(str);
    sscanf(RSTRING(str)->ptr, "%d,%d,%d,%d", &tmp.x, &tmp.y, &tmp.w, &tmp.h);
    VALUE obj = rect_alloc(klass);
    *GET_RECT(obj) = tmp;
    return obj;
}

/* ================= Table ================= */
typedef struct { int xsize, ysize, zsize; short *data; } TableVal;

static void table_free(void *p)
{
    TableVal *t = (TableVal *)p;
    if (t) { if (t->data) free(t->data); free(t); }
}
static VALUE table_alloc(VALUE k) { return Data_Wrap_Struct(k, 0, table_free, calloc(1, sizeof(TableVal))); }
#define GET_TABLE(v) ((TableVal *)DATA_PTR(v))

static int table_size(TableVal *t) { return t->xsize * t->ysize * t->zsize; }

static VALUE table_initialize(int argc, VALUE *argv, VALUE self)
{
    VALUE vx, vy, vz;
    rb_scan_args(argc, argv, "12", &vx, &vy, &vz);
    TableVal *t = GET_TABLE(self);
    t->xsize = NIL_P(vx) ? 1 : NUM2INT(vx);
    t->ysize = NIL_P(vy) ? 1 : NUM2INT(vy);
    t->zsize = NIL_P(vz) ? 1 : NUM2INT(vz);
    if (t->xsize < 1) t->xsize = 1;
    if (t->ysize < 1) t->ysize = 1;
    if (t->zsize < 1) t->zsize = 1;
    if (t->data) free(t->data);
    t->data = (short *)calloc((size_t)table_size(t), sizeof(short));
    return self;
}

static VALUE table_resize(int argc, VALUE *argv, VALUE self)
{
    return table_initialize(argc, argv, self);
}

static VALUE table_aref(int argc, VALUE *argv, VALUE self)
{
    VALUE vx, vy, vz;
    TableVal *t = GET_TABLE(self);
    rb_scan_args(argc, argv, "12", &vx, &vy, &vz);
    int x = NUM2INT(vx);
    int y = NIL_P(vy) ? 0 : NUM2INT(vy);
    int z = NIL_P(vz) ? 0 : NUM2INT(vz);
    if (x < 0 || x >= t->xsize || y < 0 || y >= t->ysize || z < 0 || z >= t->zsize)
        return INT2FIX(0);
    if (!t->data) return INT2FIX(0);
    return INT2FIX(t->data[(z * t->ysize + y) * t->xsize + x]);
}

static VALUE table_aset(int argc, VALUE *argv, VALUE self)
{
    VALUE vx, vy, vz, vval;
    TableVal *t = GET_TABLE(self);
    rb_scan_args(argc, argv, "22", &vx, &vy, &vz, &vval);
    int x = NUM2INT(vx);
    int y = 0, z = 0, val;
    if (!NIL_P(vval)) {
        y = NIL_P(vy) ? 0 : NUM2INT(vy);
        z = NIL_P(vz) ? 0 : NUM2INT(vz);
        val = NUM2INT(vval);
    } else {
        /* [x] = v 形式 */
        val = NIL_P(vy) ? 0 : NUM2INT(vy);
        y = z = 0;
    }
    if (x < 0 || x >= t->xsize || y < 0 || y >= t->ysize || z < 0 || z >= t->zsize)
        return Qnil;
    if (!t->data) return Qnil;
    t->data[(z * t->ysize + y) * t->xsize + x] = (short)val;
    return Qnil;
}

static VALUE table_xsize(VALUE self) { return INT2FIX(GET_TABLE(self)->xsize); }
static VALUE table_ysize(VALUE self) { return INT2FIX(GET_TABLE(self)->ysize); }
static VALUE table_zsize(VALUE self) { return INT2FIX(GET_TABLE(self)->zsize); }

static VALUE table_dump(VALUE self, VALUE depth)
{
    TableVal *t = GET_TABLE(self);
    (void)depth;
    int n = table_size(t);
    VALUE s = rb_str_new(NULL, 20 + n * 2);
    /* 格式: [20字节头][int16 数据] — 原版 _dump 布局 */
    char *p = RSTRING(s)->ptr;
    memcpy(p, &t->xsize, 4);
    memcpy(p + 4, &t->ysize, 4);
    memcpy(p + 8, &t->zsize, 4);
    memcpy(p + 12, &n, 4);
    memcpy(p + 16, &n, 4);   /* 原版有 20 字节头, 末 4 字节用途待定 */
    if (t->data) memcpy(p + 20, t->data, (size_t)n * 2);
    return s;
}

static VALUE table_load(VALUE klass, VALUE str)
{
    StringValue(str);
    if (RSTRING(str)->len < 20) return table_alloc(klass);
    VALUE obj = table_alloc(klass);
    TableVal *t = GET_TABLE(obj);
    const char *p = RSTRING(str)->ptr;
    int ndims = 0;
    memcpy(&ndims, p, 4);
    memcpy(&t->xsize, p + 4, 4);
    memcpy(&t->ysize, p + 8, 4);
    memcpy(&t->zsize, p + 12, 4);
    if (ndims < 3) t->zsize = 1;
    if (ndims < 2) t->ysize = 1;
    if (t->xsize < 1) t->xsize = 1;
    if (t->ysize < 1) t->ysize = 1;
    if (t->zsize < 1) t->zsize = 1;
    int n = table_size(t);
    t->data = (short *)calloc((size_t)n, sizeof(short));
    if (t->data && RSTRING(str)->len >= 20 + n * 2)
        memcpy(t->data, p + 20, (size_t)n * 2);
    return obj;
}

/* ================= Font ================= */
/* 类级默认值（原版 @@default_*） */
static VALUE s_font_default_name, s_font_default_size, s_font_default_bold,
             s_font_default_italic, s_font_default_shadow, s_font_default_color;

typedef struct { VALUE name; int size, bold, italic; VALUE color; int shadow; } FontVal;

static void font_free(void *p) { free(p); }
static void font_mark(void *p) { FontVal *f = (FontVal *)p; if (f) { rb_gc_mark(f->name); rb_gc_mark(f->color); } }
static VALUE font_alloc(VALUE k) { return Data_Wrap_Struct(k, font_mark, font_free, calloc(1, sizeof(FontVal))); }
#define GET_FONT(v) ((FontVal *)DATA_PTR(v))

static VALUE font_initialize(int argc, VALUE *argv, VALUE self)
{
    (void)argc; (void)argv;
    FontVal *f = GET_FONT(self);
    /* 原版语义: Font.new 从 Font.default_* 拷贝初值
     * (Project3 的 Main 段先设 default_name/default_size, 之后所有
     *  窗口文字都用它, 故必须读默认值而不是写死) */
    f->name   = NIL_P(s_font_default_name)  ? rb_str_new2("MS Gothic")
                                            : rb_obj_dup(s_font_default_name);
    f->size   = NIL_P(s_font_default_size)  ? 22 : NUM2INT(s_font_default_size);
    f->bold   = RTEST(s_font_default_bold)   ? 1 : 0;
    f->italic = RTEST(s_font_default_italic) ? 1 : 0;
    /* VX 引擎默认 shadow=true — 实测依据:
     * Project3 全部 95 段脚本无一处设置 font.shadow,
     * 而原版渲染的窗口文字带明显偏移阴影 => 默认即开 */
    f->shadow = NIL_P(s_font_default_shadow) ? 1 : RTEST(s_font_default_shadow);
    f->color  = Qnil;   /* 懒创建, 用 font_color() */
    return self;
}

static VALUE font_name(VALUE self) { return GET_FONT(self)->name; }
static VALUE font_name_set(VALUE self, VALUE v) { GET_FONT(self)->name = v; return v; }
static VALUE font_size(VALUE self) { return INT2FIX(GET_FONT(self)->size); }
static VALUE font_size_set(VALUE self, VALUE v) { GET_FONT(self)->size = NUM2INT(v); return v; }
static VALUE font_bold(VALUE self) { return GET_FONT(self)->bold ? Qtrue : Qfalse; }
static VALUE font_bold_set(VALUE self, VALUE v) { GET_FONT(self)->bold = RTEST(v); return v; }
static VALUE font_italic(VALUE self) { return GET_FONT(self)->italic ? Qtrue : Qfalse; }
static VALUE font_italic_set(VALUE self, VALUE v) { GET_FONT(self)->italic = RTEST(v); return v; }
static VALUE font_shadow(VALUE self) { return GET_FONT(self)->shadow ? Qtrue : Qfalse; }
static VALUE font_shadow_set(VALUE self, VALUE v) { GET_FONT(self)->shadow = RTEST(v); return v; }
/* Font#color 必须永远返回 Color 对象: 脚本写 font.color.set(...) */
static VALUE font_color(VALUE self)
{
    FontVal *f = GET_FONT(self);
    if (NIL_P(f->color)) {
        VALUE a[4] = { INT2FIX(255), INT2FIX(255), INT2FIX(255), INT2FIX(255) };
        f->color = rb_class_new_instance(4, a, cColor);
    }
    return f->color;
}
static VALUE font_color_set(VALUE self, VALUE v) { GET_FONT(self)->color = v; return v; }


static VALUE font_default_name(VALUE k) { (void)k; return s_font_default_name; }
static VALUE font_default_name_set(VALUE k, VALUE v) { (void)k; s_font_default_name = v; return v; }
static VALUE font_default_size(VALUE k) { (void)k; return s_font_default_size; }
static VALUE font_default_size_set(VALUE k, VALUE v) { (void)k; s_font_default_size = v; return v; }
static VALUE font_default_bold(VALUE k) { (void)k; return s_font_default_bold; }
static VALUE font_default_bold_set(VALUE k, VALUE v) { (void)k; s_font_default_bold = v; return v; }
static VALUE font_default_italic(VALUE k) { (void)k; return s_font_default_italic; }
static VALUE font_default_italic_set(VALUE k, VALUE v) { (void)k; s_font_default_italic = v; return v; }
static VALUE font_default_shadow(VALUE k) { (void)k; return s_font_default_shadow; }
static VALUE font_default_shadow_set(VALUE k, VALUE v) { (void)k; s_font_default_shadow = v; return v; }
static VALUE font_default_color(VALUE k) { (void)k; return s_font_default_color; }
static VALUE font_default_color_set(VALUE k, VALUE v) { (void)k; s_font_default_color = v; return v; }
static VALUE font_exist_p(VALUE k, VALUE vname) { (void)k; (void)vname; return Qtrue; }

/* ================= 注册 ================= */
void rgss_init_color(VALUE super)
{
    cColor = rb_define_class("Color", super);
    rb_define_alloc_func(cColor, color_alloc);
    rb_define_method(cColor, "initialize", color_initialize, -1);
    rb_define_method(cColor, "set", color_set, -1);
    rb_define_method(cColor, "to_s", color_to_s, 0);
    rb_define_method(cColor, "red", color_red, 0);
    rb_define_method(cColor, "red=", color_red_set, 1);
    rb_define_method(cColor, "green", color_green, 0);
    rb_define_method(cColor, "green=", color_green_set, 1);
    rb_define_method(cColor, "blue", color_blue, 0);
    rb_define_method(cColor, "blue=", color_blue_set, 1);
    rb_define_method(cColor, "alpha", color_alpha, 0);
    rb_define_method(cColor, "alpha=", color_alpha_set, 1);
    rb_define_method(cColor, "==", color_eql, 1);
    rb_define_method(cColor, "===", color_eql, 1);
    rb_define_method(cColor, "eql?", color_eql, 1);
    rb_define_method(cColor, "_dump", color_dump, 1);
    rb_define_singleton_method(cColor, "_load", color_load, 1);
}

void rgss_init_tone(VALUE super)
{
    cTone = rb_define_class("Tone", super);
    rb_define_alloc_func(cTone, tone_alloc);
    rb_define_method(cTone, "initialize", tone_initialize, -1);
    rb_define_method(cTone, "set", tone_set, -1);
    rb_define_method(cTone, "to_s", tone_to_s, 0);
    rb_define_method(cTone, "red", tone_red, 0);
    rb_define_method(cTone, "red=", tone_red_set, 1);
    rb_define_method(cTone, "green", tone_green, 0);
    rb_define_method(cTone, "green=", tone_green_set, 1);
    rb_define_method(cTone, "blue", tone_blue, 0);
    rb_define_method(cTone, "blue=", tone_blue_set, 1);
    rb_define_method(cTone, "gray", tone_gray, 0);
    rb_define_method(cTone, "gray=", tone_gray_set, 1);
    rb_define_method(cTone, "==", tone_eql, 1);
    rb_define_method(cTone, "===", tone_eql, 1);
    rb_define_method(cTone, "eql?", tone_eql, 1);
    rb_define_method(cTone, "_dump", tone_dump, 1);
    rb_define_singleton_method(cTone, "_load", tone_load, 1);
}

void rgss_init_rect(VALUE super)
{
    cRect = rb_define_class("Rect", super);
    rb_define_alloc_func(cRect, rect_alloc);
    rb_define_method(cRect, "initialize", rect_initialize, -1);
    rb_define_method(cRect, "set", rect_set, 4);
    rb_define_method(cRect, "empty", rect_empty, 0);
    rb_define_method(cRect, "to_s", rect_to_s, 0);
    rb_define_method(cRect, "x", rect_x, 0);
    rb_define_method(cRect, "x=", rect_x_set, 1);
    rb_define_method(cRect, "y", rect_y, 0);
    rb_define_method(cRect, "y=", rect_y_set, 1);
    rb_define_method(cRect, "width", rect_width, 0);
    rb_define_method(cRect, "width=", rect_width_set, 1);
    rb_define_method(cRect, "height", rect_height, 0);
    rb_define_method(cRect, "height=", rect_height_set, 1);
    rb_define_method(cRect, "==", rect_eql, 1);
    rb_define_method(cRect, "===", rect_eql, 1);
    rb_define_method(cRect, "eql?", rect_eql, 1);
    rb_define_method(cRect, "_dump", rect_dump, 1);
    rb_define_singleton_method(cRect, "_load", rect_load, 1);
}

void rgss_init_table(VALUE super)
{
    cTable = rb_define_class("Table", super);
    rb_define_alloc_func(cTable, table_alloc);
    rb_define_method(cTable, "initialize", table_initialize, -1);
    rb_define_method(cTable, "resize", table_resize, -1);
    rb_define_method(cTable, "[]", table_aref, -1);
    rb_define_method(cTable, "[]=", table_aset, -1);
    rb_define_method(cTable, "xsize", table_xsize, 0);
    rb_define_method(cTable, "ysize", table_ysize, 0);
    rb_define_method(cTable, "zsize", table_zsize, 0);
    rb_define_method(cTable, "_dump", table_dump, 1);
    rb_define_singleton_method(cTable, "_load", table_load, 1);
}

void rgss_init_font(VALUE super)
{
    cFont = rb_define_class("Font", super);
    rb_define_alloc_func(cFont, font_alloc);
    rb_define_method(cFont, "initialize", font_initialize, -1);
    rb_define_method(cFont, "name", font_name, 0);
    rb_define_method(cFont, "name=", font_name_set, 1);
    rb_define_method(cFont, "size", font_size, 0);
    rb_define_method(cFont, "size=", font_size_set, 1);
    rb_define_method(cFont, "bold", font_bold, 0);
    rb_define_method(cFont, "bold=", font_bold_set, 1);
    rb_define_method(cFont, "italic", font_italic, 0);
    rb_define_method(cFont, "italic=", font_italic_set, 1);
    rb_define_method(cFont, "shadow", font_shadow, 0);
    rb_define_method(cFont, "shadow=", font_shadow_set, 1);
    rb_define_method(cFont, "color", font_color, 0);
    rb_define_method(cFont, "color=", font_color_set, 1);

    s_font_default_name   = rb_str_new2("MS Gothic");
    s_font_default_size   = INT2FIX(20);
    s_font_default_bold   = Qfalse;
    s_font_default_italic = Qfalse;
    /* VX 引擎默认 shadow=true — 实测依据:
     * Project3 全部 95 段脚本无一处设置 font.shadow/default_shadow,
     * 而原版渲染的窗口文字带明显偏移阴影 => 默认即开 */
    s_font_default_shadow = Qtrue;
    s_font_default_color  = Qnil;
    rb_define_singleton_method(cFont, "default_name", font_default_name, 0);
    rb_define_singleton_method(cFont, "default_name=", font_default_name_set, 1);
    rb_define_singleton_method(cFont, "default_size", font_default_size, 0);
    rb_define_singleton_method(cFont, "default_size=", font_default_size_set, 1);
    rb_define_singleton_method(cFont, "default_bold", font_default_bold, 0);
    rb_define_singleton_method(cFont, "default_bold=", font_default_bold_set, 1);
    rb_define_singleton_method(cFont, "default_italic", font_default_italic, 0);
    rb_define_singleton_method(cFont, "default_italic=", font_default_italic_set, 1);
    rb_define_singleton_method(cFont, "default_shadow", font_default_shadow, 0);
    rb_define_singleton_method(cFont, "default_shadow=", font_default_shadow_set, 1);
    rb_define_singleton_method(cFont, "default_color", font_default_color, 0);
    rb_define_singleton_method(cFont, "default_color=", font_default_color_set, 1);
    rb_define_singleton_method(cFont, "exist?", font_exist_p, 1);
}

VALUE rgss_color_class(void) { return cColor; }
VALUE rgss_tone_class(void) { return cTone; }
VALUE rgss_rect_class(void) { return cRect; }
VALUE rgss_table_class(void) { return cTable; }
VALUE rgss_font_class(void) { return cFont; }
