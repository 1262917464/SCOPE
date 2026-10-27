/*
 * bitmap.c — Bitmap 类实现（重建版）
 *
 * 布局规格来源: docs/M5-c_Bitmap布局与像素格式.md（get_pixel/set_pixel 汇编解码）
 *   RData 对象:
 *     +0x0C  uint32_t *pixels   (ARGB8888 缓冲基址)
 *     +0x10  BitmapImpl *impl   (→ buf: +0x04=width, +0x08=height)
 *     +0x18  int stride         (行跨距, 字节)
 *   像素索引: pixels[(stride>>2)*y + x]; 越界静默返回 (0,0,0,0)
 */
#include <windows.h>
#include <stdint.h>
#include "ruby.h"
#include "debug.h"

typedef struct { int width, height; } PixelBuffer;
typedef struct { PixelBuffer *buf; } BitmapImpl;

typedef struct {
    uint32_t    *pixels;
    BitmapImpl  *impl;
    int          stride;
    int          disposed;
} Bitmap;

static VALUE cBitmap;
static VALUE cColor, cRect, cFont;

/* 从 Ruby 对象取 Bitmap*（Data_Wrap 风格） */
#define GET_BITMAP(v) ((Bitmap *)DATA_PTR(v))

static void bitmap_free(void *p)
{
    Bitmap *b = (Bitmap *)p;
    if (!b) return;
    if (b->pixels) free(b->pixels);
    if (b->impl) {
        if (b->impl->buf) free(b->impl->buf);
        free(b->impl);
    }
    free(b);
}

static VALUE bitmap_alloc(VALUE klass)
{
    Bitmap *b = (Bitmap *)calloc(1, sizeof(Bitmap));
    return Data_Wrap_Struct(klass, 0, bitmap_free, b);
}

/* ---------- 辅助: 取 Color 的 rgba ---------- */
static void get_color_rgba(VALUE color, int *r, int *g, int *bl, int *a)
{
    *r = *g = *bl = 0; *a = 255;
    if (NIL_P(color)) return;
    if (!rb_obj_is_kind_of(color, cColor)) return;
    *r = NUM2INT(rb_funcall(color, rb_intern("red"), 0));
    *g = NUM2INT(rb_funcall(color, rb_intern("green"), 0));
    *bl = NUM2INT(rb_funcall(color, rb_intern("blue"), 0));
    *a = NUM2INT(rb_funcall(color, rb_intern("alpha"), 0));
}

static VALUE make_color(int r, int g, int b, int a)
{
    VALUE argv[4];
    argv[0] = INT2NUM(r); argv[1] = INT2NUM(g);
    argv[2] = INT2NUM(b); argv[3] = INT2NUM(a);
    return rb_class_new_instance(4, argv, cColor);
}

/* ---------- 方法 ---------- */
/* 用 GDI+ 从文件加载图片到 ARGB8888 缓冲（Windows 自带 gdiplus） */
static int load_image_gdiplus(const char *path, uint32_t **out_bits, int *out_w, int *out_h);

static VALUE bm_initialize(int argc, VALUE *argv, VALUE self)
{
    VALUE vw, vh;
    int w = 0, h = 0;
    rb_scan_args(argc, argv, "02", &vw, &vh);

    /* Bitmap.new(path) — 图片文件加载 */
    if (!NIL_P(vw) && TYPE(vw) == T_STRING) {
        Bitmap *b = GET_BITMAP(self);
        uint32_t *bits = NULL;
        int iw = 0, ih = 0;
        fprintf(stderr, "[BM] path=%s self=%p b=%p\n", RSTRING(vw)->ptr, (void*)self, (void*)b);
        fflush(stderr);
        int ok = load_image_gdiplus(RSTRING(vw)->ptr, &bits, &iw, &ih);
        fprintf(stderr, "[BM] load=%d %dx%d bits=%p\n", ok, iw, ih, (void*)bits);
        fflush(stderr);
        if (!ok) {
            /* 加载失败: 建 1x1 空位图（原版抛异常, 这里避免崩溃） */
            iw = ih = 1;
            bits = (uint32_t *)calloc(1, 4);
        }
        b->stride = iw * 4;
        b->pixels = bits;
        b->impl = (BitmapImpl *)calloc(1, sizeof(BitmapImpl));
        b->impl->buf = (PixelBuffer *)calloc(1, sizeof(PixelBuffer));
        b->impl->buf->width = iw;
        b->impl->buf->height = ih;
        b->disposed = 0;
        if (!b->pixels)
            b->pixels = (uint32_t *)calloc(1, 4);
        {
            extern VALUE rgss_font_class(void);
            rb_iv_set(self, "@font", rb_class_new_instance(0, NULL, rgss_font_class()));
        }
        return self;
    }

    if (!NIL_P(vw)) w = NUM2INT(vw);
    if (!NIL_P(vh)) h = NUM2INT(vh);
    if (w < 0) w = 0;
    if (h < 0) h = 0;

    Bitmap *b = GET_BITMAP(self);
    b->stride = w * 4;
    b->pixels = (uint32_t *)calloc((size_t)(w * h ? w * h : 1), 4);
    b->impl = (BitmapImpl *)calloc(1, sizeof(BitmapImpl));
    b->impl->buf = (PixelBuffer *)calloc(1, sizeof(PixelBuffer));
    b->impl->buf->width = w;
    b->impl->buf->height = h;
    b->disposed = 0;
    if (!b->pixels || !b->impl || !b->impl->buf)
        rb_raise(rb_eNoMemError, "failed to create bitmap");
    {
        extern VALUE rgss_font_class(void);
        rb_iv_set(self, "@font", rb_class_new_instance(0, NULL, rgss_font_class()));
    }
    return self;
}

static VALUE bm_width(VALUE self)
{
    Bitmap *b = GET_BITMAP(self);
    if (!b->impl || !b->impl->buf) return INT2FIX(0);
    return INT2FIX(b->impl->buf->width);
}

static VALUE bm_height(VALUE self)
{
    Bitmap *b = GET_BITMAP(self);
    if (!b->impl || !b->impl->buf) return INT2FIX(0);
    return INT2FIX(b->impl->buf->height);
}

static VALUE bm_rect(VALUE self)
{
    VALUE a[4];
    a[0] = INT2FIX(0); a[1] = INT2FIX(0);
    a[2] = bm_width(self); a[3] = bm_height(self);
    return rb_class_new_instance(4, a, cRect);
}

static VALUE bm_dispose(VALUE self)
{
    Bitmap *b = GET_BITMAP(self);
    if (b->pixels) { free(b->pixels); b->pixels = NULL; }
    if (b->impl) {
        if (b->impl->buf) { free(b->impl->buf); b->impl->buf = NULL; }
    }
    b->disposed = 1;
    return Qnil;
}

static VALUE bm_disposed_p(VALUE self)
{
    Bitmap *b = GET_BITMAP(self);
    return b->disposed ? Qtrue : Qfalse;
}

static VALUE bm_get_pixel(VALUE self, VALUE vx, VALUE vy)
{
    Bitmap *b = GET_BITMAP(self);
    int x = NUM2INT(vx), y = NUM2INT(vy);
    if (!b->impl || !b->impl->buf || !b->pixels)
        rb_raise(rb_eRuntimeError, "disposed bitmap");
    if (x < 0 || y < 0 || x >= b->impl->buf->width || y >= b->impl->buf->height)
        return make_color(0, 0, 0, 0);
    uint32_t px = b->pixels[(b->stride >> 2) * y + x];
    return make_color((px >> 16) & 0xFF, (px >> 8) & 0xFF, px & 0xFF, (px >> 24) & 0xFF);
}

static VALUE bm_set_pixel(VALUE self, VALUE vx, VALUE vy, VALUE vc)
{
    Bitmap *b = GET_BITMAP(self);
    int x = NUM2INT(vx), y = NUM2INT(vy);
    int r, g, bl, a;
    if (!b->impl || !b->impl->buf || !b->pixels)
        rb_raise(rb_eRuntimeError, "disposed bitmap");
    if (x < 0 || y < 0 || x >= b->impl->buf->width || y >= b->impl->buf->height)
        return Qnil;
    get_color_rgba(vc, &r, &g, &bl, &a);
    b->pixels[(b->stride >> 2) * y + x] =
        ((uint32_t)(a & 0xFF) << 24) | ((uint32_t)(r & 0xFF) << 16) |
        ((uint32_t)(g & 0xFF) << 8)  |  (uint32_t)(bl & 0xFF);
    return Qnil;
}

static VALUE bm_clear(VALUE self)
{
    Bitmap *b = GET_BITMAP(self);
    if (!b->pixels || !b->impl || !b->impl->buf) return Qnil;
    memset(b->pixels, 0, (size_t)b->stride * b->impl->buf->height);
    return Qnil;
}

static VALUE bm_clear_rect(int argc, VALUE *argv, VALUE self)
{
    VALUE vr;
    rb_scan_args(argc, argv, "1", &vr);
    int rx = NUM2INT(rb_funcall(vr, rb_intern("x"), 0));
    int ry = NUM2INT(rb_funcall(vr, rb_intern("y"), 0));
    int rw = NUM2INT(rb_funcall(vr, rb_intern("width"), 0));
    int rh = NUM2INT(rb_funcall(vr, rb_intern("height"), 0));
    Bitmap *b = GET_BITMAP(self);
    if (!b->pixels || !b->impl || !b->impl->buf) return Qnil;
    int W = b->impl->buf->width, H = b->impl->buf->height;
    for (int y = ry; y < ry + rh; y++) {
        if (y < 0 || y >= H) continue;
        for (int x = rx; x < rx + rw; x++) {
            if (x < 0 || x >= W) continue;
            b->pixels[(b->stride >> 2) * y + x] = 0;
        }
    }
    return Qnil;
}

static VALUE bm_fill_rect(int argc, VALUE *argv, VALUE self)
{
    VALUE vx, vy, vw, vh, vc;
    rb_scan_args(argc, argv, "5", &vx, &vy, &vw, &vh, &vc);
    int rx = NUM2INT(vx), ry = NUM2INT(vy);
    int rw = NUM2INT(vw), rh = NUM2INT(vh);
    int r, g, bl, a;
    get_color_rgba(vc, &r, &g, &bl, &a);
    uint32_t px = ((uint32_t)(a & 0xFF) << 24) | ((uint32_t)(r & 0xFF) << 16) |
                  ((uint32_t)(g & 0xFF) << 8) | (uint32_t)(bl & 0xFF);
    Bitmap *b = GET_BITMAP(self);
    if (!b->pixels || !b->impl || !b->impl->buf) return Qnil;
    int W = b->impl->buf->width, H = b->impl->buf->height;
    for (int y = ry; y < ry + rh; y++) {
        if (y < 0 || y >= H) continue;
        for (int x = rx; x < rx + rw; x++) {
            if (x < 0 || x >= W) continue;
            b->pixels[(b->stride >> 2) * y + x] = px;
        }
    }
    return Qnil;
}

/* blt(dx, dy, src_bitmap, src_rect[, opacity]) */
static VALUE bm_blt(int argc, VALUE *argv, VALUE self)
{
    VALUE vdx, vdy, vsrc, vsr, vop;
    rb_scan_args(argc, argv, "41", &vdx, &vdy, &vsrc, &vsr, &vop);
    int dx = NUM2INT(vdx), dy = NUM2INT(vdy);
    int op = NIL_P(vop) ? 255 : NUM2INT(vop);
    if (op <= 0) return Qnil;
    if (!rb_obj_is_kind_of(vsrc, cBitmap)) return Qnil;

    Bitmap *dst = GET_BITMAP(self), *src = GET_BITMAP(vsrc);
    if (!dst->pixels || !dst->impl || !dst->impl->buf) return Qnil;
    if (!src->pixels || !src->impl || !src->impl->buf) return Qnil;

    int sx = NUM2INT(rb_funcall(vsr, rb_intern("x"), 0));
    int sy = NUM2INT(rb_funcall(vsr, rb_intern("y"), 0));
    int sw = NUM2INT(rb_funcall(vsr, rb_intern("width"), 0));
    int sh = NUM2INT(rb_funcall(vsr, rb_intern("height"), 0));
    int DW = dst->impl->buf->width, DH = dst->impl->buf->height;
    int SW = src->impl->buf->width, SH = src->impl->buf->height;
    int dstride = dst->stride >> 2, sstride = src->stride >> 2;

    for (int y = 0; y < sh; y++) {
        int ty = dy + y, sy2 = sy + y;
        if (ty < 0 || ty >= DH || sy2 < 0 || sy2 >= SH) continue;
        for (int x = 0; x < sw; x++) {
            int tx = dx + x, sx2 = sx + x;
            if (tx < 0 || tx >= DW || sx2 < 0 || sx2 >= SW) continue;
            uint32_t sp = src->pixels[sstride * sy2 + sx2];
            if (op < 255) {
                /* 简单按 opacity 缩放 alpha */
                uint32_t sa = (sp >> 24) & 0xFF;
                sa = sa * op / 255;
                sp = (sp & 0x00FFFFFF) | (sa << 24);
            }
            dst->pixels[dstride * ty + tx] = sp;
        }
    }
    return Qnil;
}

static VALUE bm_stretch_blt(int argc, VALUE *argv, VALUE self)
{
    VALUE vdr, vsrc, vsr, vop;
    rb_scan_args(argc, argv, "31", &vdr, &vsrc, &vsr, &vop);
    if (!rb_obj_is_kind_of(vsrc, cBitmap)) return Qnil;
    int dx = NUM2INT(rb_funcall(vdr, rb_intern("x"), 0));
    int dy = NUM2INT(rb_funcall(vdr, rb_intern("y"), 0));
    int dw = NUM2INT(rb_funcall(vdr, rb_intern("width"), 0));
    int dh = NUM2INT(rb_funcall(vdr, rb_intern("height"), 0));
    int sx = NUM2INT(rb_funcall(vsr, rb_intern("x"), 0));
    int sy = NUM2INT(rb_funcall(vsr, rb_intern("y"), 0));
    int sw = NUM2INT(rb_funcall(vsr, rb_intern("width"), 0));
    int sh = NUM2INT(rb_funcall(vsr, rb_intern("height"), 0));
    (void)vop;
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return Qnil;

    Bitmap *dst = GET_BITMAP(self), *src = GET_BITMAP(vsrc);
    if (!dst->pixels || !dst->impl || !dst->impl->buf) return Qnil;
    if (!src->pixels || !src->impl || !src->impl->buf) return Qnil;
    int DW = dst->impl->buf->width, DH = dst->impl->buf->height;
    int SW = src->impl->buf->width, SH = src->impl->buf->height;
    int dstride = dst->stride >> 2, sstride = src->stride >> 2;

    for (int y = 0; y < dh; y++) {
        int ty = dy + y;
        if (ty < 0 || ty >= DH) continue;
        int sy2 = sy + y * sh / dh;
        if (sy2 < 0 || sy2 >= SH) continue;
        for (int x = 0; x < dw; x++) {
            int tx = dx + x;
            if (tx < 0 || tx >= DW) continue;
            int sx2 = sx + x * sw / dw;
            if (sx2 < 0 || sx2 >= SW) continue;
            dst->pixels[dstride * ty + tx] = src->pixels[sstride * sy2 + sx2];
        }
    }
    return Qnil;
}

/* ---------- 字体创建: 照原版 0x129A0 ----------
 * 0x12A17  lstrcpynW(lfFaceName, names[idx], 32)   名字来自 add_name 槽数组
 * 0x12A1D  lfHeight = [CFont+0x358]                (Font#size, 默认 20)
 * 0x12A2D  lfWidth  = 0
 * 0x12A37  CreateFontIndirectW
 * 0x12A55  fld1 / fcomp [CFont+0x360]              ← 字体缩放, 构造时置 1.0
 * 0x12A69  jnp → 0x12ADA                           ← **相等就直接返回**
 * 0x12A78  仅当缩放 != 1.0 时才 GetTextMetricsW 并重算:
 *          0x12AA1  fild lfHeight
 *          0x12AA7  fimul tmAveCharWidth
 *          0x12AAB  fidiv tmHeight
 *          0x12AAF  fmul [CFont+0x360]
 *          0x12AB4  lfWidth = round(上面的积)
 *          0x12AB5  CreateFontIndirectW(重建)
 *
 * 关键: 0x12A69 的 jnp 在“缩放 == 1.0”时跳到 0x12ADA 直接返回第一遍
 * 的字体, lfWidth 保持 0。游戏全程不改缩放 (没有任何代码写 0x360 成
 * 非 1.0 的值), 所以原版**永远走 lfWidth=0 的路径**。
 *
 * lfWidth != 0 会让 GDI 把字形横向拉伸到指定宽度, 笔画随之变粗。
 * 早期实现无条件做这段修正 (22×10/20 = 11), 正是重建版文字偏粗的根因。 */
static HFONT rgss_create_font(const wchar_t *face, int size, int bold, int italic)
{
    LOGFONTW lf;
    memset(&lf, 0, sizeof lf);
    lf.lfHeight = size;                    /* 0x12A1D: 直接用 Font#size */
    lf.lfWidth  = 0;                       /* 0x12A2D */
    lf.lfCharSet = DEFAULT_CHARSET;        /* 原版构造函数: lfCharSet=1 */
    /* 原版 lfQuality (CFont+0x316) 从未被写入, 保持 0 = DEFAULT_QUALITY */
    lf.lfQuality = DEFAULT_QUALITY;
    if (bold) lf.lfWeight = FW_BOLD;
    if (italic) lf.lfItalic = TRUE;
    lstrcpynW(lf.lfFaceName, face, LF_FACESIZE);

    /* 0x12A37: 第一遍创建即最终字体 (缩放恒 1.0) */
    return CreateFontIndirectW(&lf);
}

/* 系统字体族集合 —— 原版 0x13450 构造 + 0x13330 回调
 *
 * 原版在引擎初始化时 (0x10001238) 构造一个字体注册对象并存到
 * RGSS 单例 +0x260 (0x1000124a), 内容是:
 *   0x13450: GetDC(NULL) + LOGFONT{lfCharSet=DEFAULT_CHARSET}
 *            → EnumFontFamiliesExW(…, 0x10013330, this, 0)
 *   0x13330: 对每个 ENUMLOGFONTEXW 取其 lfFaceName (结构 +0x1c 起),
 *            构造 wstring 后插入 std::set (0x12da0)
 * 这个集合是**系统真实可用的字体族名**全集, 不做字符集/字形过滤。
 *
 * 随后 Font#name= 的 setter (0x10007524 循环 / 0x1000755e、0x100075ad)
 * 遍历 Ruby 侧给的名字数组, 只把**命中集合**的名字送进 add_name
 * (0x12FE0 查集合 → 非零才 add_name):
 *   0x12860 add_name: n = [this]; if (n >= 10) return;
 *                     lstrcpynW(this + n*64 + 4, name, 32);
 *                     [this] = n + 1; [+0x36c] = 1;
 * 于是 CFont 里最多 10 个候选名, 按 Ruby 数组顺序排列。
 *
 * 最后 0x129A0(idx) 用第 idx 个候选名建字体, 失败(返回 NULL)时
 * 调用方 (0x15FF7) 把 idx 加 1 重试, 最多 10 次。没有“名字不存在就
 * 换下一个”的判断 —— 换名完全由 CreateFontIndirectW 成功与否驱动。
 */
#define RGSS_FONT_MAX_NAMES 10
/* 系统字体族集合容量。本机实测 5570 个族, 留足余量。
 * (早期写成 MAX_NAMES*4=40 导致集合被截断, 未命中的名字反而被当成
 *  "可用" 交给 GDI, GDI 又回退到宋体 —— 这是字体选错的直接原因) */
#define RGSS_FACE_SET_CAP   16384

static wchar_t g_face_set[RGSS_FACE_SET_CAP][LF_FACESIZE];
static int g_face_set_n;

static int CALLBACK rgss_enum_face_cb(const LOGFONTW *lf, const TEXTMETRICW *tm,
                                      DWORD type, LPARAM lp)
{
    (void)tm; (void)type; (void)lp;
    if (!lf || !lf->lfFaceName[0]) return 1;
    /* 跳过以 '@' 开头的竖排字体族 (原版集合里也有, 但字体名不会写成 @xxx) */
    if (g_face_set_n < (int)(sizeof g_face_set / sizeof g_face_set[0])) {
        lstrcpynW(g_face_set[g_face_set_n], lf->lfFaceName, LF_FACESIZE);
        g_face_set_n++;
    }
    return 1;
}

/* 建集合 —— 原版 0x13450 */
static void font_build_face_set(void)
{
    LOGFONTW lf;
    HDC dc;
    if (g_face_set_n > 0) return;
    memset(&lf, 0, sizeof lf);
    lf.lfCharSet = DEFAULT_CHARSET;      /* 0x10013495: [esp+0x37]=1 */
    dc = GetDC(NULL);
    if (dc) {
        EnumFontFamiliesExW(dc, &lf, (FONTENUMPROCW)rgss_enum_face_cb, 0, 0);
        ReleaseDC(NULL, dc);
    }
}

/* 名字是否在集合里 —— 原版 0x12FE0 (有序查找, 此处线性等价) */
static int font_face_available(const wchar_t *name)
{
    int i;
    font_build_face_set();
    for (i = 0; i < g_face_set_n; i++)
        if (lstrcmpiW(g_face_set[i], name) == 0) return 1;
    return 0;
}

/* 从 Font#name 生成候选名列表 —— 原版 setter (0x10007524) + add_name (0x12860)
 * 返回候选个数; out 里按 Ruby 数组顺序存最多 10 个命中集合的名字。 */
static int font_collect_names(VALUE vname, wchar_t out[][LF_FACESIZE])
{
    VALUE names = vname, tmp = Qnil;
    long n, i;
    int cnt = 0;

    if (NIL_P(vname)) return 0;
    if (TYPE(names) != T_ARRAY) {
        tmp = rb_ary_new3(1, names);
        names = tmp;
    }
    n = RARRAY(names)->len;
    for (i = 0; i < n && cnt < RGSS_FONT_MAX_NAMES; i++) {
        VALUE e = RARRAY(names)->ptr[i];
        wchar_t cand[LF_FACESIZE];
        int nl;
        if (TYPE(e) != T_STRING || RSTRING(e)->len == 0) continue;
        nl = MultiByteToWideChar(CP_UTF8, 0, RSTRING(e)->ptr,
                                 (int)RSTRING(e)->len, NULL, 0);
        if (nl <= 0 || nl >= LF_FACESIZE) continue;
        MultiByteToWideChar(CP_UTF8, 0, RSTRING(e)->ptr,
                            (int)RSTRING(e)->len, cand, LF_FACESIZE - 1);
        cand[nl] = 0;
        /* 0x12FE0: 不在集合里就跳过, 不占名额 */
        if (!font_face_available(cand)) continue;
        lstrcpynW(out[cnt], cand, LF_FACESIZE);
        cnt++;
    }
    return cnt;
}

/* 按原版 0x15FF7 的“候选名 + 索引”方式建字体:
 * 从第 0 个命中集合的候选名开始尝试 CreateFontIndirectW, 成功即用,
 * 失败则索引 +1 重试, 最多 RGSS_FONT_MAX_NAMES 次 (0x1604C 的 +1 与 10 上限)。
 * 返回实际使用的候选名 (供调用方回读/记录) 和字体句柄。 */
static HFONT rgss_create_font_from_names(VALUE vname, int size, int bold, int italic,
                                        wchar_t *face_out)
{
    wchar_t cands[RGSS_FONT_MAX_NAMES][LF_FACESIZE];
    int n = font_collect_names(vname, cands);
    int i;
    HFONT hf;

    if (n == 0) {
        /* 原版集合为空 / 名字全被过滤: 用 Ruby 侧的第一个名字兜底,
         * 让 GDI 自行替换 (与原版直接交给 CreateFontIndirectW 一致) */
        if (!NIL_P(vname)) {
            VALUE first = (TYPE(vname) == T_ARRAY && RARRAY(vname)->len > 0)
                          ? RARRAY(vname)->ptr[0] : vname;
            if (TYPE(first) == T_STRING) {
                int nl = MultiByteToWideChar(CP_UTF8, 0, RSTRING(first)->ptr,
                                             (int)RSTRING(first)->len, NULL, 0);
                if (nl > 0 && nl < LF_FACESIZE) {
                    MultiByteToWideChar(CP_UTF8, 0, RSTRING(first)->ptr,
                                        (int)RSTRING(first)->len,
                                        cands[0], LF_FACESIZE - 1);
                    cands[0][nl] = 0;
                    n = 1;
                }
            }
        }
        if (n == 0) lstrcpyW(cands[0], L"MS Gothic"), n = 1;
    }

    for (i = 0; i < n; i++) {
        hf = rgss_create_font(cands[i], size, bold, italic);
        if (hf) {
            if (face_out) lstrcpyW(face_out, cands[i]);
            return hf;
        }
    }
    /* 全部失败: 用最后一个候选名 (GDI 必回的兜底字体) */
    if (face_out) lstrcpyW(face_out, cands[n - 1]);
    return rgss_create_font(cands[n - 1], size, bold, italic);
}

/* ------------------------------------------------------------------
 * 旧实现 (DrawTextW + GetTextExtentPoint32W)，已被下方 GetGlyphOutlineW
 * 自绘路径取代。按决策整体移出编译，留作对照参考。
 * 原版 draw_text=0x15EB0 / text_size=0x15CC0 均为 GetGlyphOutlineW 自绘，
 * 详见 docs/M5-j_draw_text逆向.md
 * ------------------------------------------------------------------ */
#if 0
static VALUE bm_draw_text(int argc, VALUE *argv, VALUE self)
{
    VALUE vx, vy, vw, vh, vstr, valign;
    int x, y, w, h;
    /* 两种形式: draw_text(x,y,w,h,str[,align]) 或 draw_text(rect, str[,align]) */
    if (argc >= 2 && rb_obj_is_kind_of(argv[0], rgss_rect_class())) {
        VALUE vr = argv[0];
        x = NUM2INT(rb_funcall(vr, rb_intern("x"), 0));
        y = NUM2INT(rb_funcall(vr, rb_intern("y"), 0));
        w = NUM2INT(rb_funcall(vr, rb_intern("width"), 0));
        h = NUM2INT(rb_funcall(vr, rb_intern("height"), 0));
        vstr = argv[1];
        valign = (argc >= 3) ? argv[2] : Qnil;
    } else {
        rb_scan_args(argc, argv, "51", &vx, &vy, &vw, &vh, &vstr, &valign);
        x = NUM2INT(vx); y = NUM2INT(vy);
        w = NUM2INT(vw); h = NUM2INT(vh);
    }
    (void)valign;
    Bitmap *b = GET_BITMAP(self);
    if (!b->pixels || !b->impl || !b->impl->buf)
        rb_raise(rb_eRuntimeError, "disposed bitmap");
    if (NIL_P(vstr)) return Qnil;
    /* 原版接受任意对象(金钱数字等)并内部调 to_s; StringValue 会直接抛 TypeError */
    if (TYPE(vstr) != T_STRING)
        vstr = rb_funcall(vstr, rb_intern("to_s"), 0);

    int W = b->impl->buf->width, H = b->impl->buf->height;

    /* 用 GDI 渲染到临时 DIB, 再拷贝（保持与 Nx 后端一致的像素语义） */
    HDC hdc = CreateCompatibleDC(NULL);
    if (!hdc) return Qnil;
    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof bi);
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w > 0 ? w : 1;
    bi.bmiHeader.biHeight = -(h > 0 ? h : 1);   /* top-down */
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void *bits = NULL;
    HBITMAP hbm = CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!hbm || !bits) { DeleteDC(hdc); return Qnil; }
    HGDIOBJ old = SelectObject(hdc, hbm);

    /* 取 Font 设置（若 bitmap 有 font 属性） */
    int fsize = 20, bold = 0, italic = 0, shadow = 0;
    int fr = 255, fg = 255, fb = 255, fa = 255;
    VALUE vfont = rb_iv_get(self, "@font");
    if (!NIL_P(vfont)) {
        VALUE vs = rb_funcall(vfont, rb_intern("size"), 0);
        if (!NIL_P(vs)) fsize = NUM2INT(vs);
        bold = RTEST(rb_funcall(vfont, rb_intern("bold"), 0));
        italic = RTEST(rb_funcall(vfont, rb_intern("italic"), 0));
        shadow = RTEST(rb_funcall(vfont, rb_intern("shadow"), 0));
        /* 颜色: Font#color -> Color(red, green, blue, alpha) */
        VALUE vc = rb_funcall(vfont, rb_intern("color"), 0);
        if (!NIL_P(vc)) {
            VALUE t;
            t = rb_funcall(vc, rb_intern("red"), 0);   if (!NIL_P(t)) fr = NUM2INT(t);
            t = rb_funcall(vc, rb_intern("green"), 0); if (!NIL_P(t)) fg = NUM2INT(t);
            t = rb_funcall(vc, rb_intern("blue"), 0);  if (!NIL_P(t)) fb = NUM2INT(t);
            t = rb_funcall(vc, rb_intern("alpha"), 0); if (!NIL_P(t)) fa = NUM2INT(t);
        }
    }
    /* 字体名: 取 Font#name 数组, 按原版 0x12FE0 集合过滤后逐个尝试建字体 */
    wchar_t facename[LF_FACESIZE];
    VALUE vn = NIL_P(vfont) ? Qnil : rb_funcall(vfont, rb_intern("name"), 0);
    HFONT hf = rgss_create_font_from_names(vn, fsize, bold, italic, facename);
    HGDIOBJ oldf = SelectObject(hdc, hf);
    {
        static int dbg = 0;
        if (dbg++ < 2) {
            wchar_t face[256]; char fu8[512];
            GetTextFaceW(hdc, 256, face);
            WideCharToMultiByte(CP_UTF8, 0, face, -1, fu8, 512, NULL, NULL);
            TEXTMETRICW tm; GetTextMetricsW(hdc, &tm);
            FILE *f = RGSS_LOG_FOPEN("rgss_text.log");
            if (f) {
                fprintf(f, "[font] size=%d face=%s tmh=%ld\n",
                        fsize, fu8, (long)tm.tmHeight);
                fclose(f);
            }
        }
    }
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));

    /* UTF-8 → UTF-16（RGSS 字符串是 UTF-8, GDI 需要宽字符） */
    int srclen = (int)RSTRING(vstr)->len;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, RSTRING(vstr)->ptr, srclen, NULL, 0);
    WCHAR *wstr = NULL;
    if (wlen > 0) {
        wstr = (WCHAR *)malloc((wlen + 1) * sizeof(WCHAR));
        if (wstr) {
            MultiByteToWideChar(CP_UTF8, 0, RSTRING(vstr)->ptr, srclen, wstr, wlen);
            wstr[wlen] = 0;
        }
    }
    RECT rc = { 0, 0, w, h };
    if (wstr) {
        /* RGSS draw_text 语义: 在矩形内垂直居中 (24px 行高放 16px 字 => 偏移 4px,
 *  与原版文字顶部 y=308 实测吻合), 必须 DT_SINGLELINE 才能用 DT_VCENTER */
        DrawTextW(hdc, wstr, wlen, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        free(wstr);
    }

    /* 阴影层: Font#shadow 为真时, 再渲一份黑字偏移 (1,1)。
     * 注意 hbm2 必须活到合成结束, 否则 sbits 被释放 */
    uint32_t *shadow_bits = NULL;
    HDC hdc2 = NULL; HBITMAP hbm2 = NULL; HGDIOBJ old2 = NULL;
    if (shadow && wstr) {
        BITMAPINFO bi2;
        ZeroMemory(&bi2, sizeof bi2);
        bi2.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi2.bmiHeader.biWidth = w > 0 ? w : 1;
        bi2.bmiHeader.biHeight = -(h > 0 ? h : 1);
        bi2.bmiHeader.biPlanes = 1;
        bi2.bmiHeader.biBitCount = 32;
        bi2.bmiHeader.biCompression = BI_RGB;
        void *sbits = NULL;
        hdc2 = CreateCompatibleDC(NULL);
        hbm2 = CreateDIBSection(hdc2, &bi2, DIB_RGB_COLORS, &sbits, NULL, 0);
        if (hbm2 && sbits) {
            old2 = SelectObject(hdc2, hbm2);
            SelectObject(hdc2, hf);
            SetBkMode(hdc2, TRANSPARENT);
            /* 必须用白字渲覆盖度: 黑字 RGB 全 0, 覆盖度算出来恒为 0 */
            SetTextColor(hdc2, RGB(255, 255, 255));
            RECT rc2 = { 1, 1, w, h };
            WCHAR *w2 = (WCHAR *)malloc((wlen + 1) * sizeof(WCHAR));
            if (w2) {
                MultiByteToWideChar(CP_UTF8, 0, RSTRING(vstr)->ptr, srclen, w2, wlen);
                w2[wlen] = 0;
                DrawTextW(hdc2, w2, wlen, &rc2, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                free(w2);
            }
            shadow_bits = (uint32_t *)sbits;
            {
                static int dbg = 0;
                if (dbg++ < 2) {
                    FILE *f = RGSS_LOG_FOPEN("rgss_text.log");
                    if (f) {
                        fprintf(f, "[txt] w=%d h=%d wlen=%d sbits=%p font=%s size=%d color=(%d,%d,%d,%d)\n",
                                w, h, wlen, (void *)sbits, facename, fsize, fr, fg, fb, fa);
                        fclose(f);
                    }
                }
            }
        }
    }

    /* 拷入目标: GDI 白字渲在零缓冲上, 亮度即覆盖度;
     * 颜色用 Font#color, 阴影先画 (黑, 半透明) */
    int dstride = b->stride >> 2;
    uint32_t *src = (uint32_t *)bits;
    for (int yy = 0; yy < h; yy++) {
        int ty = y + yy;
        if (ty < 0 || ty >= H) continue;
        for (int xx = 0; xx < w; xx++) {
            int tx = x + xx;
            if (tx < 0 || tx >= W) continue;
            uint32_t sp = src[yy * w + xx];
            uint32_t cov = ((sp & 0xFF) + ((sp >> 8) & 0xFF) + ((sp >> 16) & 0xFF)) / 3;
            if (cov > 0) {
                uint32_t a = (uint32_t)cov * (uint32_t)fa / 255;
                b->pixels[dstride * ty + tx] =
                    ((a & 0xFF) << 24) | ((fb & 0xFF) << 16) | ((fg & 0xFF) << 8) | (fr & 0xFF);
            } else if (shadow_bits) {
                uint32_t ss = shadow_bits[yy * w + xx];
                uint32_t sc = ((ss & 0xFF) + ((ss >> 8) & 0xFF) + ((ss >> 16) & 0xFF)) / 3;
                if (sc > 0) {
                    /* 原版阴影为不透明黑 (实测最暗 b=0), 仅抗锯齿边缘由覆盖度决定透明度 */
                    uint32_t a = sc;
                    b->pixels[dstride * ty + tx] = (a << 24);
                }
            }
        }
    }

    if (hdc2 && old2) SelectObject(hdc2, old2);
    if (hbm2) DeleteObject(hbm2);
    if (hdc2) DeleteDC(hdc2);
    SelectObject(hdc, oldf);
    DeleteObject(hf);
    SelectObject(hdc, old);
    DeleteObject(hbm);
    DeleteDC(hdc);
    return Qnil;
}

/* text_size(str) -> Rect(width, height) */
static VALUE bm_text_size(VALUE self, VALUE vstr)
{
    StringValue(vstr);
    int fsize = 20, bold = 0, italic = 0;
    VALUE vfont = rb_iv_get(self, "@font");
    if (!NIL_P(vfont)) {
        VALUE vs = rb_funcall(vfont, rb_intern("size"), 0);
        if (!NIL_P(vs)) fsize = NUM2INT(vs);
        bold = RTEST(rb_funcall(vfont, rb_intern("bold"), 0));
        italic = RTEST(rb_funcall(vfont, rb_intern("italic"), 0));
    }
    /* 字体名解析与 draw_text 同源 (原版 0x12FE0 集合过滤 + 索引重试) */
    wchar_t facename[LF_FACESIZE];
    VALUE vn = NIL_P(vfont) ? Qnil : rb_funcall(vfont, rb_intern("name"), 0);
    HDC hdc = CreateCompatibleDC(NULL);
    HFONT hf = rgss_create_font_from_names(vn, fsize, bold, italic, facename);
    HGDIOBJ oldf = SelectObject(hdc, hf);
    SIZE sz = { 0, 0 };
    {
        int sl = (int)RSTRING(vstr)->len;
        int wl = MultiByteToWideChar(CP_UTF8, 0, RSTRING(vstr)->ptr, sl, NULL, 0);
        WCHAR *ws = (WCHAR *)malloc((wl + 1) * sizeof(WCHAR));
        if (ws) {
            MultiByteToWideChar(CP_UTF8, 0, RSTRING(vstr)->ptr, sl, ws, wl);
            GetTextExtentPoint32W(hdc, ws, wl, &sz);
            free(ws);
        }
    }
    SelectObject(hdc, oldf);
    DeleteObject(hf);
    DeleteDC(hdc);
    VALUE a[4] = { INT2FIX(0), INT2FIX(0), INT2FIX(sz.cx), INT2FIX(sz.cy) };
    return rb_class_new_instance(4, a, cRect);
}
#endif

/* ============ 自绘字形光栅化 (原版 0x15EB0 draw_text / 0x15CC0 text_size) ============
 * 原版无 TextOut/DrawText 导入, 每字符经 GetGlyphOutlineW(GGO_GRAY8_BITMAP)
 * 取 8bit 灰度字形, 灰度转 alpha (64→255, 其余×4) 后两遍合成:
 *   阴影 = 同一 alpha 图偏移 (+1,+1), 黑色, alpha 用字体颜色的 alpha
 *   正文 = alpha 图原位, 字体颜色
 * 混合器原版为 0x96680 (模式分发), 此处只还原 text 用到的 alpha+纯色 模式。 */

/* 取一个字符的 GGO_GRAY8_BITMAP 灰度位图
 * 对应原版 0x1602C (查询) / 0x160A8 (取位图) / 0x16048 (重试 10 次)
 * 返回缓冲区大小 (0=失败); 成功时 *pbuf 需 free */
static int rgss_get_glyph(HDC hdc, wchar_t ch, GLYPHMETRICS *pgm, unsigned char **pbuf)
{
    static const MAT2 ident = { {0,1},{0,0},{0,0},{0,1} };  /* 原版 0x15CEB/0x15FC8 */
    int retry;
    for (retry = 0; retry < 10; retry++) {
        GLYPHMETRICS gm;
        int sz = GetGlyphOutlineW(hdc, ch, 6 /*GGO_GRAY8_BITMAP*/,
                                  &gm, 0, NULL, &ident);
        if (sz < 0) return 0;
        /* 空格特判 (原版 0x16032-0x16041): 0x20 / U+3000 → size+1 */
        if (ch == 0x20 || ch == 0x3000) sz += 1;
        if (sz > 0) {
            /* 原版 0x16077: lea 0x4(ebx,ebx,1) → size*2+4 */
            unsigned char *buf = (unsigned char *)calloc(1, (size_t)sz * 2 + 4);
            if (!buf) return 0;
            if (!GetGlyphOutlineW(hdc, ch, 6, &gm, sz, buf, &ident)) {
                free(buf); return 0;
            }
            *pgm = gm; *pbuf = buf;
            return sz;
        }
        /* size==0 → 重试 (原版 0x16048); 空格加 1 后仍 0 → 放弃 */
        if (ch == 0x20 || ch == 0x3000) break;
    }
    return 0;
}

/* 逐字符渲染 alpha 图 (原版 0x15FA0-0x16170)
 * amap: 8bit alpha 图, 尺寸 aw×ah; (ox0,oy0) = 基线起点在位图坐标系的位置
 * 返回: 终点笔宽 (用于对齐) */
static int rgss_render_alpha(HDC hdc, const WCHAR *ws, int len,
                             unsigned char *amap, int aw, int ah,
                             int ox0, int oy0, int fsize, int tmH)
{
    MAT2 mat = { {0,1},{0,0},{0,0},{0,1} };
    int pen = 0, i;
    for (i = 0; i < len; i++) {
        GLYPHMETRICS gm;
        unsigned char *buf = NULL;
        int sz = rgss_get_glyph(hdc, ws[i], &gm, &buf);
        int cell = 0;
        if (sz > 0 && buf) {
            /* 行距 = (gmBlackBoxX+3) & ~3 (原版 0x160D0) */
            int gstride = (gm.gmBlackBoxX + 3) & ~3;
            int gx = ox0 + pen + gm.gmptGlyphOrigin.x;
            int gy = oy0 - gm.gmptGlyphOrigin.y;
            int row, col;
            for (row = 0; row < gm.gmBlackBoxY; row++) {
                const unsigned char *src = buf + (size_t)row * gstride;
                for (col = 0; col < gm.gmBlackBoxX; col++) {
                    int g = src[col];
                    int a, ax, ay;
                    /* 灰度→alpha (原版 0x1610A/0x16119): 64→255, 其余×4 */
                    if (g == 0x40) a = 255;
                    else if (g > 0) a = g * 4;
                    else continue;
                    ax = gx + col; ay = gy + row;
                    if (ax < 0 || ax >= aw || ay < 0 || ay >= ah) continue;
                    amap[(size_t)ay * aw + ax] = (unsigned char)a;
                }
            }
            free(buf);
        }
        cell = gm.gmCellIncX;
        /* MS Gothic 兼容修正 (原版 0x1614D-0x16169 与 0x15DAB-0x15DB0) */
        if (fsize == 20 && cell == 19 && tmH == 20) cell = 18;
        pen += cell;
    }
    return pen;
}

/* alpha 图 → 位图写入 —— 照原版混合器逐通道公式精确还原
 *
 * 白盒依据 (原版反汇编):
 *   0x1009FE00  MMX 文字混合内循环 (map 变体):
 *                 a  = LUT[map_byte]            (纯文字 LUT=恒等)
 *                 f  = F[a]                     (16bit 因子)
 *                 out = dst + (src−dst)*f >> 8  (RGBA 四通道)
 *   0x100A9790  F 表初始化: F[0]=0, F[a]=a+1 (0x100A9820 循环, +0x10001 步进)
 *   0x161F6/0x16214  draw_text 两遍 blit: 阴影 src=(0,0,0,字体alpha),
 *                 正文 src=(字体色,字体alpha)
 * 所以权重 = (a+1)/256 而非 a/255; 字体 alpha 不乘进 a, 而是作为 src 的
 * alpha 通道参与混合 —— 覆盖度只决定混合权重。
 * 效果: AA 边缘像素 = 白压黑(阴影在下面), 比白压背景更暗, 与原版一致。 */
static void rgss_write_over(Bitmap *b, const unsigned char *amap, int aw, int ah,
                            int dx, int dy,
                            int cx0, int cy0, int cw, int chh,
                            int cr, int cg, int cb, int alpha)
{
    int W = b->impl->buf->width, H = b->impl->buf->height;
    int stride = b->stride >> 2;
    int x, y;
    /* 源色按字体 alpha 预乘: 原版的纯色 src tile 是用同一混合器画到透明
     * 底上生成的 (rgb = color×fa/255, a = fa), 本函数等价于对该 tile
     * 做 map 混合。fa=255 时不变; fa=128 (禁用项) 时 rgb=128 —— 由此
     * contents 为一致预乘形式, 预乘 over 合成后才与原版一致
     * (实测禁用项字芯 155 = 128 + bg×0.498, 而非预乘时的饱和 255)。 */
    int sr = cr * alpha / 255;
    int sg = cg * alpha / 255;
    int sb = cb * alpha / 255;
    for (y = 0; y < ah; y++) {
        int ty = dy + y;
        if (ty < 0 || ty >= H || ty < cy0 || ty >= cy0 + chh) continue;
        for (x = 0; x < aw; x++) {
            int tx = dx + x;
            int a, f, dr, dg, db, da;
            uint32_t px, *dp;
            if (tx < 0 || tx >= W || tx < cx0 || tx >= cx0 + cw) continue;
            a = amap[(size_t)y * aw + x];
            if (!a) continue;              /* F[0]=0 → 原样 (0x100A97F7) */
            f = a + 1;                     /* F[a]=a+1 (0x100A9820) */
            dp = &b->pixels[(size_t)stride * ty + tx];
            px = *dp;
            da = (int)((px >> 24) & 0xFF);
            dr = (int)((px >> 16) & 0xFF);
            dg = (int)((px >> 8) & 0xFF);
            db = (int)(px & 0xFF);
            /* 四通道同式: out = dst + (src−dst)*f >> 8 (pmullw+psrlw 语义) */
            {
                int or_ = dr + (((sr - dr) * f) >> 8);
                int og = dg + (((sg - dg) * f) >> 8);
                int ob = db + (((sb - db) * f) >> 8);
                int oa = da + (((alpha - da) * f) >> 8);
                if (or_ < 0) or_ = 0; else if (or_ > 255) or_ = 255;
                if (og < 0) og = 0; else if (og > 255) og = 255;
                if (ob < 0) ob = 0; else if (ob > 255) ob = 255;
                if (oa < 0) oa = 0; else if (oa > 255) oa = 255;
                *dp = ((uint32_t)oa << 24) | ((uint32_t)or_ << 16)
                    | ((uint32_t)og << 8) | (uint32_t)ob;
            }
        }
    }
}

/* 度量: 原版 0x15CC0 — 宽 = Σ修正后 cellIncX, 高 = max(tmHeight) */
static int rgss_measure_text(HDC hdc, const WCHAR *ws, int len, int fsize, int tmH)
{
    int total = 0, i;
    for (i = 0; i < len; i++) {
        GLYPHMETRICS gm;
        unsigned char *dmy = NULL;
        int sz = rgss_get_glyph(hdc, ws[i], &gm, &dmy);
        int cell = (sz > 0) ? gm.gmCellIncX : 0;
        if (fsize == 20 && cell == 19 && tmH == 20) cell = 18;
        total += cell;
        if (dmy) free(dmy);
    }
    return total;
}

/* draw_text(x, y, width, height, str[, align]) — 原版 0x15EB0 还原 */
static VALUE bm_draw_text(int argc, VALUE *argv, VALUE self)
{
    VALUE vx, vy, vw, vh, vstr, valign;
    int x, y, w, h;
    /* 两种形式: draw_text(x,y,w,h,str[,align]) 或 draw_text(rect, str[,align]) */
    if (argc >= 2 && rb_obj_is_kind_of(argv[0], rgss_rect_class())) {
        VALUE vr = argv[0];
        x = NUM2INT(rb_funcall(vr, rb_intern("x"), 0));
        y = NUM2INT(rb_funcall(vr, rb_intern("y"), 0));
        w = NUM2INT(rb_funcall(vr, rb_intern("width"), 0));
        h = NUM2INT(rb_funcall(vr, rb_intern("height"), 0));
        vstr = argv[1];
        valign = (argc >= 3) ? argv[2] : Qnil;
    } else {
        rb_scan_args(argc, argv, "51", &vx, &vy, &vw, &vh, &vstr, &valign);
        x = NUM2INT(vx); y = NUM2INT(vy);
        w = NUM2INT(vw); h = NUM2INT(vh);
    }
    if (NIL_P(vstr)) return Qnil;
    /* 原版接受任意对象(金钱数字等)并内部调 to_s */
    if (TYPE(vstr) != T_STRING)
        vstr = rb_funcall(vstr, rb_intern("to_s"), 0);

    Bitmap *b = GET_BITMAP(self);
    if (!b->pixels || !b->impl || !b->impl->buf)
        rb_raise(rb_eRuntimeError, "disposed bitmap");

    /* 字体设置 */
    int fsize = 20, bold = 0, italic = 0, shadow = 0;
    int fr = 255, fg = 255, fb = 255, fa = 255;
    VALUE vfont = rb_iv_get(self, "@font");
    if (!NIL_P(vfont)) {
        VALUE vs = rb_funcall(vfont, rb_intern("size"), 0);
        if (!NIL_P(vs)) fsize = NUM2INT(vs);
        bold = RTEST(rb_funcall(vfont, rb_intern("bold"), 0));
        italic = RTEST(rb_funcall(vfont, rb_intern("italic"), 0));
        shadow = RTEST(rb_funcall(vfont, rb_intern("shadow"), 0));
        VALUE vc = rb_funcall(vfont, rb_intern("color"), 0);
        if (!NIL_P(vc)) {
            VALUE t;
            t = rb_funcall(vc, rb_intern("red"), 0);   if (!NIL_P(t)) fr = NUM2INT(t);
            t = rb_funcall(vc, rb_intern("green"), 0); if (!NIL_P(t)) fg = NUM2INT(t);
            t = rb_funcall(vc, rb_intern("blue"), 0);  if (!NIL_P(t)) fb = NUM2INT(t);
            t = rb_funcall(vc, rb_intern("alpha"), 0); if (!NIL_P(t)) fa = NUM2INT(t);
        }
    }
    wchar_t facename[LF_FACESIZE];
    VALUE vn = NIL_P(vfont) ? Qnil : rb_funcall(vfont, rb_intern("name"), 0);
    HFONT hf = rgss_create_font_from_names(vn, fsize, bold, italic, facename);
    {
        static int dbg = 0;
        if (dbg++ < 6) {
            FILE *f = RGSS_LOG_FOPEN("rgss_text.log");
            if (f) {
                char fu8[256];
                WideCharToMultiByte(CP_UTF8, 0, facename, -1, fu8, 256, NULL, NULL);
                fprintf(f, "[draw] face=%s size=%d color=(%d,%d,%d,%d) shadow=%d\n",
                        fu8, fsize, fr, fg, fb, fa, shadow);
                fclose(f);
            }
        }
    }
    if (!hf) return Qnil;
    HDC hdc = CreateCompatibleDC(NULL);
    HGDIOBJ oldf = SelectObject(hdc, hf);
    {
        static int dbg2 = 0;
        if (dbg2++ < 4) {
            /* 实测诊断: GDI 实际解析到的字面 + GGO 字形墨量 */
            FILE *f = RGSS_LOG_FOPEN("rgss_text.log");
            if (f) {
                wchar_t act[LF_FACESIZE] = {0};
                char au8[256] = {0};
                TEXTMETRICW tm0;
                MAT2 mt = { {0,1},{0,0},{0,0},{0,1} };
                GLYPHMETRICS g0;
                long ink = 0;
                unsigned bbw = 0, bbh = 0;
                long cellx = 0;
                GetTextFaceW(hdc, LF_FACESIZE, act);
                WideCharToMultiByte(CP_UTF8, 0, act, -1, au8, 256, NULL, NULL);
                GetTextMetricsW(hdc, &tm0);
                DWORD sz = GetGlyphOutlineW(hdc, L'\u7279', 6, &g0, 0, NULL, &mt);
                if (sz != GDI_ERROR && sz) {
                    BYTE *buf = (BYTE *)calloc(1, sz);
                    if (GetGlyphOutlineW(hdc, L'\u7279', 6, &g0, sz, buf, &mt)) {
                        int st = (g0.gmBlackBoxX + 3) & ~3;
                        for (unsigned i = 0; i < g0.gmBlackBoxY; i++)
                            for (int j = 0; j < (int)g0.gmBlackBoxX; j++)
                                ink += buf[i * st + j];
                    }
                    free(buf);
                }
                bbw = g0.gmBlackBoxX; bbh = g0.gmBlackBoxY; cellx = g0.gmCellIncX;
                fprintf(f, "[FACE] req_sz=%d actual=%s tmH=%ld cellX=%ld bb=%ux%u ink=%ld\n",
                        fsize, au8, (long)tm0.tmHeight, cellx, bbw, bbh, ink);
                fclose(f);
            }
        }
    }

    int srclen = (int)RSTRING(vstr)->len;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, RSTRING(vstr)->ptr, srclen, NULL, 0);
    WCHAR *ws = NULL;
    unsigned char *amap = NULL;
    if (wlen > 0) {
        TEXTMETRICW tm;
        GetTextMetricsW(hdc, &tm);
        int tmH = tm.tmHeight;
        ws = (WCHAR *)malloc((wlen + 1) * sizeof(WCHAR));
        if (ws) {
            int align;
            int total;
            int aw, ah, baseline, startx;
            unsigned char *amap2;
            MultiByteToWideChar(CP_UTF8, 0, RSTRING(vstr)->ptr, srclen, ws, wlen);
            ws[wlen] = 0;
            /* 度量 (原版 0x15CC0): 对齐用 */
            total = rgss_measure_text(hdc, ws, wlen, fsize, tmH);
            align = NIL_P(valign) ? 0 : NUM2INT(valign);
            startx = x;
            if (align == 1) startx = x + (w - total) / 2;
            else if (align == 2) startx = x + w - total;
            if (startx < x) startx = x;

            /* alpha 图: 覆盖 (x-2,y-2) 起 (w+4)×(h+4), 容纳阴影 +1,+1 与 origin */
            aw = w + 4; ah = h + 4;
            amap2 = (unsigned char *)calloc(1, (size_t)aw * ah);
            if (amap2) {
                /* 垂直居中: 原版字形顶 = rect.y + (rect.h - 字形高)/2 */
                baseline = y + (h + tmH) / 2 - tm.tmDescent;
                rgss_render_alpha(hdc, ws, wlen, amap2, aw, ah,
                                  startx - (x - 2), baseline - (y - 2),
                                  fsize, tmH);
                /* 两遍合成 (原版 0x161A9-0x16214)
                 * 原版语义: REPLACE — 直接写 ARGB, 不与已有像素混合。
                 * 这样 refresh 的 alpha=255 绘制会被 draw_item(1,false) 的
                 * alpha=128 绘制完全替换, 禁用项才不会发黑。
                 * 位图的 alpha 字段由窗口合成 (rgss_render_blit) 消费。 */
                if (shadow)
                    rgss_write_over(b, amap2, aw, ah, x - 2 + 1, y - 2 + 1,
                                       x, y, w, h, 0, 0, 0, fa);
                rgss_write_over(b, amap2, aw, ah, x - 2, y - 2,
                                   x, y, w, h, fr, fg, fb, fa);
                free(amap2);
            }
        }
        free(ws);
    }
    SelectObject(hdc, oldf);
    DeleteObject(hf);
    DeleteDC(hdc);
    return Qnil;
}

/* text_size(str) -> Rect(width, height) — 原版 0x15CC0 还原 */
static VALUE bm_text_size(VALUE self, VALUE vstr)
{
    StringValue(vstr);
    int fsize = 20, bold = 0, italic = 0;
    VALUE vfont = rb_iv_get(self, "@font");
    if (!NIL_P(vfont)) {
        VALUE vs = rb_funcall(vfont, rb_intern("size"), 0);
        if (!NIL_P(vs)) fsize = NUM2INT(vs);
        bold = RTEST(rb_funcall(vfont, rb_intern("bold"), 0));
        italic = RTEST(rb_funcall(vfont, rb_intern("italic"), 0));
    }
    wchar_t facename[LF_FACESIZE];
    VALUE vn = NIL_P(vfont) ? Qnil : rb_funcall(vfont, rb_intern("name"), 0);
    HFONT hf = rgss_create_font_from_names(vn, fsize, bold, italic, facename);
    int total = 0, tmH = 0;
    if (hf) {
        HDC hdc = CreateCompatibleDC(NULL);
        HGDIOBJ oldf = SelectObject(hdc, hf);
        TEXTMETRICW tm;
        GetTextMetricsW(hdc, &tm);
        tmH = tm.tmHeight;
        StringValue(vstr);
        {
            int srclen = (int)RSTRING(vstr)->len;
            int wlen = MultiByteToWideChar(CP_UTF8, 0, RSTRING(vstr)->ptr,
                                           srclen, NULL, 0);
            if (wlen > 0) {
                WCHAR *ws = (WCHAR *)malloc((wlen + 1) * sizeof(WCHAR));
                if (ws) {
                    MultiByteToWideChar(CP_UTF8, 0, RSTRING(vstr)->ptr,
                                        srclen, ws, wlen);
                    ws[wlen] = 0;
                    total = rgss_measure_text(hdc, ws, wlen, fsize, tmH);
                    free(ws);
                }
            }
        }
        SelectObject(hdc, oldf);
        DeleteDC(hdc);
    }
    if (hf) DeleteObject(hf);
    {
        VALUE a[4] = { INT2FIX(0), INT2FIX(0), INT2FIX(total), INT2FIX(tmH) };
        return rb_class_new_instance(4, a, cRect);
    }
}


static VALUE bm_font_get(VALUE self)
{
    VALUE f = rb_iv_get(self, "@font");
    if (NIL_P(f)) {
        /* 原版: 未设置时返回默认 Font 实例 */
        extern VALUE rgss_font_class(void);
        f = rb_class_new_instance(0, NULL, rgss_font_class());
        rb_iv_set(self, "@font", f);
    }
    return f;
}
static VALUE bm_font_set(VALUE self, VALUE v) { rb_iv_set(self, "@font", v); return v; }

static VALUE bm_hue_change(VALUE self, VALUE vh) { (void)self; (void)vh; return Qnil; }
static VALUE bm_blur(VALUE self) { return Qnil; }
static VALUE bm_radial_blur(VALUE self, VALUE va, VALUE vr) { (void)va; (void)vr; return Qnil; }

static VALUE bm_gradient_fill_rect(int argc, VALUE *argv, VALUE self)
{
    VALUE vx, vy, vw, vh, c1, c2, vertical;
    rb_scan_args(argc, argv, "61", &vx, &vy, &vw, &vh, &c1, &c2, &vertical);
    int rx = NUM2INT(vx), ry = NUM2INT(vy);
    int rw = NUM2INT(vw), rh = NUM2INT(vh);
    int r1, g1, b1, a1, r2, g2, b2, a2;
    get_color_rgba(c1, &r1, &g1, &b1, &a1);
    get_color_rgba(c2, &r2, &g2, &b2, &a2);
    int vert = RTEST(vertical);
    Bitmap *b = GET_BITMAP(self);
    if (!b->pixels || !b->impl || !b->impl->buf) return Qnil;
    int W = b->impl->buf->width, H = b->impl->buf->height;
    int dstride = b->stride >> 2;
    int span = vert ? (rh > 1 ? rh - 1 : 1) : (rw > 1 ? rw - 1 : 1);
    for (int y = 0; y < rh; y++) {
        int ty = ry + y;
        if (ty < 0 || ty >= H) continue;
        for (int x = 0; x < rw; x++) {
            int tx = rx + x;
            if (tx < 0 || tx >= W) continue;
            int t = vert ? y : x;
            int rr = r1 + (r2 - r1) * t / span;
            int gg = g1 + (g2 - g1) * t / span;
            int bb = b1 + (b2 - b1) * t / span;
            int aa = a1 + (a2 - a1) * t / span;
            b->pixels[dstride * ty + tx] = ((uint32_t)(aa & 0xFF) << 24) |
                ((uint32_t)(rr & 0xFF) << 16) | ((uint32_t)(gg & 0xFF) << 8) |
                (uint32_t)(bb & 0xFF);
        }
    }
    return Qnil;
}

void rgss_init_bitmap(VALUE super)
{
    cBitmap = rb_define_class("Bitmap", super);
    rb_define_alloc_func(cBitmap, bitmap_alloc);
    rb_define_method(cBitmap, "initialize", bm_initialize, -1);
    rb_define_method(cBitmap, "width", bm_width, 0);
    rb_define_method(cBitmap, "height", bm_height, 0);
    rb_define_method(cBitmap, "rect", bm_rect, 0);
    rb_define_method(cBitmap, "dispose", bm_dispose, 0);
    rb_define_method(cBitmap, "disposed?", bm_disposed_p, 0);
    rb_define_method(cBitmap, "get_pixel", bm_get_pixel, 2);
    rb_define_method(cBitmap, "set_pixel", bm_set_pixel, 3);
    rb_define_method(cBitmap, "clear", bm_clear, 0);
    rb_define_method(cBitmap, "clear_rect", bm_clear_rect, -1);
    rb_define_method(cBitmap, "fill_rect", bm_fill_rect, -1);
    rb_define_method(cBitmap, "gradient_fill_rect", bm_gradient_fill_rect, -1);
    rb_define_method(cBitmap, "blt", bm_blt, -1);
    rb_define_method(cBitmap, "stretch_blt", bm_stretch_blt, -1);
    rb_define_method(cBitmap, "draw_text", bm_draw_text, -1);
    rb_define_method(cBitmap, "text_size", bm_text_size, 1);
    rb_define_method(cBitmap, "font", bm_font_get, 0);
    rb_define_method(cBitmap, "font=", bm_font_set, 1);
    rb_define_method(cBitmap, "hue_change", bm_hue_change, 1);
    rb_define_method(cBitmap, "blur", bm_blur, 0);
    rb_define_method(cBitmap, "radial_blur", bm_radial_blur, 2);
}

/* 供 classes.c 使用 */
VALUE rgss_bitmap_class(void) { return cBitmap; }
void rgss_set_color_class(VALUE c) { cColor = c; }
void rgss_set_rect_class(VALUE r) { cRect = r; }

/* ================= GDI+ 图片加载（C 接口） ================= */
/* gdiplus 的 C 接口在 C 模式下不可用, 改用 C++ 单元实现 (image.cpp) */
extern int rgss_load_image(const char *path, unsigned int **out_bits, int *out_w, int *out_h);

static int load_image_gdiplus(const char *path, uint32_t **out_bits, int *out_w, int *out_h)
{
    return rgss_load_image(path, (unsigned int **)out_bits, out_w, out_h);
}
