/*
 * rgss_graphics_classes.c — Graphics / Sprite / Viewport / Plane / Tilemap /
 *                           Window / Input / Audio 实现（重建版）
 * 依据: docs/M5-c_图形类注册链.md + docs/M5-c_Bitmap布局与像素格式.md
 *       328 条 API 映射表 (docs/assets/rgss_ruby_api_map.tsv)
 */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "ruby.h"

VALUE rgss_bitmap_class(void);
VALUE rgss_color_class(void);
VALUE rgss_rect_class(void);
VALUE rgss_font_class(void);

static VALUE mGraphics, mInput, mAudio;
void rgss_sprite_register(void *s);
void rgss_sprite_unregister(void *s);
static VALUE cSprite, cViewport, cPlane, cTilemap, cWindow;
/* Tilemap 注册表 (前向声明, 定义见下方) */
#define MAX_TILEMAPS 8
static void *g_tilemaps[MAX_TILEMAPS];
static int g_tilemap_count;
static void tilemap_register(void *t);
static void tilemap_unregister(void *tp);
static void tilemap_free2(void *p);


/* 全局图形状态（原版 CGameFrame 字段语义） */
static int   g_frame_rate = 60;
static int   g_frame_count = 0;
static int   g_brightness = 255;
static int   g_frozen = 0;
static int   g_screen_w = 544, g_screen_h = 416;
static HWND  g_hwnd;

/* 渲染管线（rgss_render.c） */
int  rgss_render_init(HWND hwnd, int w, int h);
void rgss_render_shutdown(void);
void rgss_render_present(void);
void rgss_render_clear(uint32_t argb);
void *rgss_render_get_bits(void);
int  rgss_render_width(void);
int  rgss_render_height(void);
typedef struct { uint32_t *bits; int w, h, stride; } SrcSurface;
void rgss_render_blit(const SrcSurface *src, int dst_x, int dst_y,
                      int src_x, int src_y, int src_w, int src_h,
                      int dst_w, int dst_h, int opacity, int blend_type);
extern VALUE rgss_bitmap_class(void);

/* ================= Graphics ================= */
long rgss_sprite_count_dbg(void);
static VALUE gfx_update(VALUE mod)
{
    (void)mod;
    MSG msg;
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    /* 帧率限制: 原版按 frame_rate 限帧 (默认 60)。
     * 没有限帧时游戏跑满 CPU, 且光标闪烁等帧同步效果完全失真 */
    {
        static int limiter_init = 0;
        static LARGE_INTEGER qpf, next;
        if (!limiter_init) {
            limiter_init = 1;
            QueryPerformanceFrequency(&qpf);
            QueryPerformanceCounter(&next);
        }
        long long step = qpf.QuadPart / (g_frame_rate > 0 ? g_frame_rate : 60);
        next.QuadPart += step;
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        if (next.QuadPart > now.QuadPart) {
            long long us = (next.QuadPart - now.QuadPart) * 1000000LL / qpf.QuadPart;
            if (us > 1500) Sleep((DWORD)(us / 1000) - 1);
            for (;;) {
                QueryPerformanceCounter(&now);
                if (now.QuadPart >= next.QuadPart) break;
            }
        } else {
            /* 落后过多: 重新对齐, 不追帧 */
            next.QuadPart = now.QuadPart + step;
        }
    }
    /* 注意: Graphics.freeze 只是快照当前画面用于转场, 不停止 update */
    g_frame_count++;
    rgss_render_clear(0xFF000000u);
    rgss_compose_scene();
    rgss_render_present();
    if (g_frame_count <= 3 || (g_frame_count % 60) == 0) {
        FILE *f = fopen("rgss_render.log", "a");
        if (f) { fprintf(f, "[gfx] frame=%d bits=%p sprites=%d win=0x%X\n",
                         g_frame_count, rgss_render_get_bits(),
                         (int)rgss_sprite_count_dbg(), (unsigned)g_hwnd); fclose(f); }
    }
    /* 诊断: 每 5000 帧记录场景名, 确认主循环归属 */
    if (g_frame_count == 5000) {
        VALUE sc = rb_gv_get("$scene");
        FILE *f = fopen("rgss_caller.log", "a");
        if (f) {
            fprintf(f, "[caller] frame=%d scene=%s nil=%d\n", g_frame_count,
                    NIL_P(sc) ? "nil" : rb_obj_classname(sc), NIL_P(sc));
            fclose(f);
        }
    }
    return Qnil;
}

static VALUE gfx_frame_rate(VALUE m) { (void)m; return INT2FIX(g_frame_rate); }
static VALUE gfx_frame_rate_set(VALUE m, VALUE v) { (void)m; g_frame_rate = NUM2INT(v); return v; }
static VALUE gfx_frame_count(VALUE m) { (void)m; return INT2FIX(g_frame_count); }
static VALUE gfx_frame_count_set(VALUE m, VALUE v)
{
    (void)m;
    g_frame_count = NUM2INT(v);
    return INT2FIX(g_frame_count);
}
static VALUE gfx_frame_reset(VALUE m) { (void)m; g_frame_count = 0; return Qnil; }
static VALUE gfx_brightness(VALUE m) { (void)m; return INT2FIX(g_brightness); }
static VALUE gfx_brightness_set(VALUE m, VALUE v) { (void)m; g_brightness = NUM2INT(v); return v; }
/* freeze: 保存当前画面快照（供 transition 淡入淡出使用） */
static VALUE gfx_freeze(VALUE m)
{
    (void)m;
    g_frozen = 1;
    /* TODO: 把后缓冲拷到独立快照缓冲, 供 transition 使用 */
    return Qnil;
}
static VALUE gfx_transition(VALUE m, int argc, VALUE *argv)
{
    (void)m; (void)argc; (void)argv;
    gfx_update(m);
    return Qnil;
}
static VALUE gfx_wait(VALUE m, VALUE vframes)
{
    int n = NUM2INT(vframes);
    for (int i = 0; i < n; i++) gfx_update(m);
    return Qnil;
}
static VALUE gfx_fadeout(VALUE m, VALUE vframes) { (void)m; (void)vframes; return Qnil; }
static VALUE gfx_fadein(VALUE m, VALUE vframes) { (void)m; (void)vframes; return Qnil; }
static VALUE gfx_snap_to_bitmap(VALUE m)
{
    (void)m;
    VALUE args[2] = { INT2FIX(g_screen_w), INT2FIX(g_screen_h) };
    VALUE bmp = rb_class_new_instance(2, args, rgss_bitmap_class());
    return bmp;
}
static VALUE gfx_resize_screen(VALUE m, VALUE vw, VALUE vh)
{
    (void)m;
    g_screen_w = NUM2INT(vw);
    g_screen_h = NUM2INT(vh);
    return Qnil;
}
/* 小工具: 往日志文件追加一行 (变参), 返回是否成功 */
static int f_open_log(const char *path, const char *fmt, ...)
{
    FILE *f = fopen(path, "a");
    if (!f) return 0;
    va_list ap; va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc(10, f);
    fclose(f);
    return 1;
}

void rgss_compose_scene(void);

static VALUE gfx_reset(VALUE m) { (void)m; g_frame_count = 0; g_frozen = 0; return Qnil; }

/* ================= Input =================
 * 依据原版 CInput 布局 (0x138a0 构造函数 + 0x13940 采样 + 0x13c40 更新):
 *   +0x00 vftable              +0x04 hwnd
 *   +0x18 键号→内部槽位映射表(25 字节)
 *   +0x31 采样缓冲[30]         +0x4f 当前帧状态[30]
 *   +0x6d 上一帧状态[30]
 *   +0x8c 连发键号 +0x90 连发计数 +0x94 dir4 +0x98 dir8
 * 键号是 RGSS 自定义常量 (2/4/6/8 为方向), 不是 VK 码;
 * 构造函数把键号翻译成内部槽位, 采样函数按槽位写状态。
 */

/* 键号常量 (原版实测: 0x10104294..0x101042d0 处的常量名) */
enum {
    RGSS_KEY_DOWN = 2,  RGSS_KEY_LEFT = 4,
    RGSS_KEY_RIGHT = 6, RGSS_KEY_UP = 8,
    RGSS_KEY_A = 11, RGSS_KEY_B = 12, RGSS_KEY_C = 13,
    RGSS_KEY_X = 14, RGSS_KEY_Y = 15, RGSS_KEY_Z = 16,
    RGSS_KEY_L = 17, RGSS_KEY_R = 18,
    RGSS_KEY_SHIFT = 21, RGSS_KEY_CTRL = 22, RGSS_KEY_ALT = 23,
    RGSS_KEY_F5 = 25, RGSS_KEY_F6 = 26, RGSS_KEY_F7 = 27,
    RGSS_KEY_F8 = 28, RGSS_KEY_F9 = 29,
    RGSS_KEY_NUM = 30
};

/* 键号 → 主 VK 码 (辅键在 rgss_poll_key 里另行判断) */
static const int g_key_vk[RGSS_KEY_NUM] = {
    /*  0 */ 0,
    /*  1 */ 0,
    /*  2 DOWN  */ VK_DOWN,
    /*  3 */ 0,
    /*  4 LEFT  */ VK_LEFT,
    /*  5 */ 0,
    /*  6 RIGHT */ VK_RIGHT,
    /*  7 */ 0,
    /*  8 UP    */ VK_UP,
    /*  9 */ 0,
    /* 10 */ 0,
    /* 11 A */ 'Z',
    /* 12 B */ 'X',
    /* 13 C */ 'C',
    /* 14 X */ 'A',
    /* 15 Y */ 'S',
    /* 16 Z */ 'D',
    /* 17 L */ 'Q',
    /* 18 R */ 'W',
    /* 19 */ 0,
    /* 20 */ 0,
    /* 21 SHIFT */ VK_SHIFT,
    /* 22 CTRL  */ VK_CONTROL,
    /* 23 ALT   */ VK_MENU,
    /* 24 */ 0,
    /* 25 F5 */ VK_F5,
    /* 26 F6 */ VK_F6,
    /* 27 F7 */ VK_F7,
    /* 28 F8 */ VK_F8,
    /* 29 F9 */ VK_F9
};

static unsigned char g_key_now[RGSS_KEY_NUM];
static unsigned char g_key_prev[RGSS_KEY_NUM];
static int g_rep_key = -1;
static int g_rep_count = 0;
static int g_dir4 = 0, g_dir8 = 0;

/* 单键采样: 原版对每个键号检查若干物理键, 任一按下即置位 */
static int rgss_poll_key(int key)
{
    int vk;
    if (key <= 0 || key >= RGSS_KEY_NUM) return 0;
    vk = g_key_vk[key];
    if (vk && (GetAsyncKeyState(vk) & 0x8000)) return 1;

    switch (key) {
    /* 方向键: 主键 + 小键盘 (原版 0x62/0x64/0x66/0x68) */
    case RGSS_KEY_DOWN:  return (GetAsyncKeyState(VK_NUMPAD2) & 0x8000) != 0;
    case RGSS_KEY_LEFT:  return (GetAsyncKeyState(VK_NUMPAD4) & 0x8000) != 0;
    case RGSS_KEY_RIGHT: return (GetAsyncKeyState(VK_NUMPAD6) & 0x8000) != 0;
    case RGSS_KEY_UP:    return (GetAsyncKeyState(VK_NUMPAD8) & 0x8000) != 0;
    /* C 键: 额外接受 Enter/Space (原版 0x0d/0x20) */
    case RGSS_KEY_C:
        return (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0 ||
               (GetAsyncKeyState(VK_SPACE)  & 0x8000) != 0;
    /* B 键: 额外接受 Esc (原版 0x1b) */
    case RGSS_KEY_B:
        return (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
    /* L/R: 额外接受 PageUp/PageDown (原版 0x21/0x22) */
    case RGSS_KEY_L: return (GetAsyncKeyState(VK_PRIOR) & 0x8000) != 0;
    case RGSS_KEY_R: return (GetAsyncKeyState(VK_NEXT)  & 0x8000) != 0;
    /* SHIFT/CTRL/ALT: 左右都算 */
    case RGSS_KEY_SHIFT:
        return (GetAsyncKeyState(VK_LSHIFT) & 0x8000) || (GetAsyncKeyState(VK_RSHIFT) & 0x8000);
    case RGSS_KEY_CTRL:
        return (GetAsyncKeyState(VK_LCONTROL) & 0x8000) || (GetAsyncKeyState(VK_RCONTROL) & 0x8000);
    case RGSS_KEY_ALT:
        return (GetAsyncKeyState(VK_LMENU) & 0x8000) || (GetAsyncKeyState(VK_RMENU) & 0x8000);
    default:
        return 0;
    }
}

/* Input.update — 对应原版 0x13c40: 采样一帧并推算 dir4/dir8/连发 */
static VALUE input_update(VALUE m)
{
    int i;
    (void)m;
    /* 上一帧 = 本帧 (原版 +0x4f → +0x6d 的 rep movsl) */
    memcpy(g_key_prev, g_key_now, sizeof(g_key_now));

    for (i = 1; i < RGSS_KEY_NUM; i++)
        g_key_now[i] = (unsigned char)rgss_poll_key(i);

    /* 连发: 有新的按下键就切换, 否则给当前键累加计数 (原版 0x13c85 段) */
    {
        int fresh = -1;
        for (i = 1; i < RGSS_KEY_NUM; i++) {
            if (g_key_now[i] && !g_key_prev[i]) { fresh = i; break; }
        }
        if (fresh >= 0) {
            g_rep_key = fresh;
            g_rep_count = 1;
        } else if (g_rep_key >= 0 && g_key_now[g_rep_key]) {
            g_rep_count++;
        } else {
            g_rep_key = -1;
            g_rep_count = 0;
        }
    }

    /* dir4 / dir8 — 原版 0x13cee 起: 先算竖直/水平分量再合成 */
    {
        int up    = g_key_now[RGSS_KEY_UP]    ? 1 : 0;
        int down  = g_key_now[RGSS_KEY_DOWN]  ? 1 : 0;
        int left  = g_key_now[RGSS_KEY_LEFT]  ? 1 : 0;
        int right = g_key_now[RGSS_KEY_RIGHT] ? 1 : 0;
        int v = up - down;
        int h = right - left;

        g_dir8 = 0;
        if (v > 0)      g_dir8 = (h > 0) ? 9 : (h < 0) ? 7 : 8;
        else if (v < 0) g_dir8 = (h > 0) ? 3 : (h < 0) ? 1 : 2;
        else if (h > 0) g_dir8 = 6;
        else if (h < 0) g_dir8 = 4;

        /* dir4 优先竖直分量 (原版 0x13d65 段) */
        g_dir4 = 0;
        if (v > 0)      g_dir4 = 8;
        else if (v < 0) g_dir4 = 2;
        else if (h > 0) g_dir4 = 6;
        else if (h < 0) g_dir4 = 4;
    }

    /* 诊断: 有按键时写一行日志, 便于外部验证采样是否生效 */
    {
        static long call_count = 0;
        int any = 0;
        call_count++;
        for (i = 1; i < RGSS_KEY_NUM; i++) if (g_key_now[i]) { any = i; break; }
        if (any) {
            static int last_any = -1;
            if (any != last_any || g_rep_count <= 3) {
                FILE *f = fopen("rgss_input.log", "a");
                if (f) {
                    fprintf(f, "[in] #%ld keys=", call_count);
                    for (i = 1; i < RGSS_KEY_NUM; i++)
                        if (g_key_now[i]) fprintf(f, "%d,", i);
                    fprintf(f, " dir4=%d dir8=%d rep=%d/%d\n",
                            g_dir4, g_dir8, g_rep_key, g_rep_count);
                    fclose(f);
                }
            }
            last_any = any;
        } else if (call_count == 1 || (call_count % 300) == 0) {
            FILE *f = fopen("rgss_input.log", "a");
            if (f) {
                fprintf(f, "[in] #%ld idle (update 被调用, 无按键)\n", call_count);
                fclose(f);
            }
        }
    }
    return Qnil;
}

static VALUE input_press_p(VALUE m, VALUE vk)
{
    int key = NUM2INT(vk);
    (void)m;
    if (key <= 0 || key >= RGSS_KEY_NUM) return Qfalse;
    return g_key_now[key] ? Qtrue : Qfalse;
}

static VALUE input_trigger_p(VALUE m, VALUE vk)
{
    int key = NUM2INT(vk);
    (void)m;
    if (key <= 0 || key >= RGSS_KEY_NUM) return Qfalse;
    /* 原版 0x136a0: 本帧按下且上一帧未按下 */
    return (g_key_now[key] && !g_key_prev[key]) ? Qtrue : Qfalse;
}

static VALUE input_repeat_p(VALUE m, VALUE vk)
{
    int key = NUM2INT(vk);
    (void)m;
    /* 原版 0x136d0: 只认当前连发键;
     * 计数为 1 (首帧) 或 >=24 且能被 6 整除时为真 */
    if (key <= 0 || key >= RGSS_KEY_NUM) return Qfalse;
    if (key != g_rep_key) return Qfalse;
    if (g_rep_count == 1) return Qtrue;
    if (g_rep_count >= 24 && (g_rep_count % 6) == 0) return Qtrue;
    return Qfalse;
}

static VALUE input_dir4(VALUE m) { (void)m; return INT2FIX(g_dir4); }
static VALUE input_dir8(VALUE m) { (void)m; return INT2FIX(g_dir8); }

/* ================= Audio (简化: MCI) ================= */
static VALUE audio_play(VALUE m, int argc, VALUE *argv) { (void)m; (void)argc; (void)argv; return Qnil; }
static VALUE audio_stop(VALUE m) { (void)m; return Qnil; }
static VALUE audio_fade(VALUE m, VALUE t) { (void)m; (void)t; return Qnil; }

/* ================= Sprite ================= */
typedef struct {
    VALUE viewport, bitmap, src_rect, color, tone;
    double x, y, z, ox, oy, zoom_x, zoom_y, angle;
    int visible, opacity, blend_type, mirror;
    int wave_amp, wave_length, wave_speed, wave_phase;
    int bush_depth, bush_opacity;
    int disposed;
} SpriteVal;

static void sprite_free(void *p) { if (p) rgss_sprite_unregister(p); free(p); }
/* GC 标记: 结构体内保存的 VALUE 必须标, 否则 GC 会回收仍在使用的对象 */
static void sprite_mark(void *p)
{
    SpriteVal *s = (SpriteVal *)p;
    if (!s) return;
    rb_gc_mark(s->viewport); rb_gc_mark(s->bitmap); rb_gc_mark(s->src_rect);
    rb_gc_mark(s->color); rb_gc_mark(s->tone);
}
static VALUE sprite_alloc(VALUE k) { return Data_Wrap_Struct(k, sprite_mark, sprite_free, calloc(1, sizeof(SpriteVal))); }
#define GET_SPRITE(v) ((SpriteVal *)DATA_PTR(v))

static VALUE sprite_initialize(int argc, VALUE *argv, VALUE self)
{
    VALUE vp;
    SpriteVal *s = GET_SPRITE(self);
    rb_scan_args(argc, argv, "01", &vp);
    /* calloc 让 VALUE 字段为 0, 即 Qfalse 而非 Qnil, 必须显式初始化 */
    s->viewport = vp;
    s->bitmap   = Qnil;
    /* src_rect 默认必须是全零 Rect 对象: 脚本会直接 src_rect.set(...) */
    {
        VALUE ra[4] = { INT2FIX(0), INT2FIX(0), INT2FIX(0), INT2FIX(0) };
        s->src_rect = rb_class_new_instance(4, ra, rgss_rect_class());
    }
    s->color    = Qnil;
    s->tone     = Qnil;
    s->visible = 1;
    s->opacity = 255;
    s->zoom_x = s->zoom_y = 1.0;
    s->bush_opacity = 128;
    rgss_sprite_register(s);
    return self;
}

static VALUE sprite_dispose(VALUE self) { GET_SPRITE(self)->disposed = 1; return Qnil; }
static VALUE sprite_disposed_p(VALUE self) { return GET_SPRITE(self)->disposed ? Qtrue : Qfalse; }

#define SPR_ACC(name, field, type, conv, rconv) \
    static VALUE sprite_##name(VALUE s) { return rconv(GET_SPRITE(s)->field); } \
    static VALUE sprite_##name##_set(VALUE s, VALUE v) { GET_SPRITE(s)->field = conv(v); return v; }

SPR_ACC(x, x, double, NUM2DBL, rb_float_new)
SPR_ACC(y, y, double, NUM2DBL, rb_float_new)
SPR_ACC(z, z, double, NUM2DBL, rb_float_new)
SPR_ACC(ox, ox, double, NUM2DBL, rb_float_new)
SPR_ACC(oy, oy, double, NUM2DBL, rb_float_new)
SPR_ACC(zoom_x, zoom_x, double, NUM2DBL, rb_float_new)
SPR_ACC(zoom_y, zoom_y, double, NUM2DBL, rb_float_new)
SPR_ACC(angle, angle, double, NUM2DBL, rb_float_new)
SPR_ACC(opacity, opacity, int, NUM2INT, INT2FIX)
SPR_ACC(blend_type, blend_type, int, NUM2INT, INT2FIX)
SPR_ACC(wave_amp, wave_amp, int, NUM2INT, INT2FIX)
SPR_ACC(wave_length, wave_length, int, NUM2INT, INT2FIX)
SPR_ACC(wave_speed, wave_speed, int, NUM2INT, INT2FIX)
SPR_ACC(wave_phase, wave_phase, int, NUM2INT, INT2FIX)
SPR_ACC(bush_depth, bush_depth, int, NUM2INT, INT2FIX)
SPR_ACC(bush_opacity, bush_opacity, int, NUM2INT, INT2FIX)

static VALUE sprite_visible(VALUE s) { return GET_SPRITE(s)->visible ? Qtrue : Qfalse; }
static VALUE sprite_visible_set(VALUE s, VALUE v) { GET_SPRITE(s)->visible = RTEST(v); return v; }
static VALUE sprite_mirror(VALUE s) { return GET_SPRITE(s)->mirror ? Qtrue : Qfalse; }
static VALUE sprite_mirror_set(VALUE s, VALUE v) { GET_SPRITE(s)->mirror = RTEST(v); return v; }

static VALUE sprite_bitmap(VALUE s) { return GET_SPRITE(s)->bitmap; }
static VALUE sprite_bitmap_set(VALUE s, VALUE v)
{
    SpriteVal *sp = GET_SPRITE(s);
    sp->bitmap = v;
    /* 原版语义: 设置 bitmap 时 src_rect 自动重置为整张图 */
    if (!NIL_P(v)) {
        VALUE w = rb_funcall(v, rb_intern("width"), 0);
        VALUE h = rb_funcall(v, rb_intern("height"), 0);
        VALUE ra[4] = { INT2FIX(0), INT2FIX(0), w, h };
        sp->src_rect = rb_class_new_instance(4, ra, rgss_rect_class());
    }
    return v;
}
/* src_rect 必须始终是 Rect 对象: 脚本普遍写 obj.src_rect.set(...)。
 * 原版即使未设 bitmap 也返回一个全零 Rect, 这里懒创建保证永不为 nil。 */
static VALUE sprite_src_rect(VALUE s)
{
    SpriteVal *sp = GET_SPRITE(s);
    if (NIL_P(sp->src_rect)) {
        VALUE ra[4] = { INT2FIX(0), INT2FIX(0), INT2FIX(0), INT2FIX(0) };
        sp->src_rect = rb_class_new_instance(4, ra, rgss_rect_class());
    }
    return sp->src_rect;
}
static VALUE sprite_src_rect_set(VALUE s, VALUE v) { GET_SPRITE(s)->src_rect = v; return v; }
static VALUE sprite_viewport(VALUE s) { return GET_SPRITE(s)->viewport; }
static VALUE sprite_viewport_set(VALUE s, VALUE v) { GET_SPRITE(s)->viewport = v; return v; }
/* sprite 的 color/tone 也必须始终是对象: 脚本写 self.color.set(...) */
static VALUE sprite_color(VALUE s)
{
    SpriteVal *sp = GET_SPRITE(s);
    if (NIL_P(sp->color)) {
        VALUE a[4] = { INT2FIX(0), INT2FIX(0), INT2FIX(0), INT2FIX(255) };
        sp->color = rb_class_new_instance(4, a, rgss_color_class());
    }
    return sp->color;
}
static VALUE sprite_color_set(VALUE s, VALUE v) { GET_SPRITE(s)->color = v; return v; }
static VALUE sprite_tone(VALUE s)
{
    SpriteVal *sp = GET_SPRITE(s);
    if (NIL_P(sp->tone)) {
        VALUE a[4] = { INT2FIX(0), INT2FIX(0), INT2FIX(0), INT2FIX(0) };
        sp->tone = rb_class_new_instance(4, a, rgss_tone_class());
    }
    return sp->tone;
}
static VALUE sprite_tone_set(VALUE s, VALUE v) { GET_SPRITE(s)->tone = v; return v; }
static VALUE sprite_flash(VALUE s, VALUE c, VALUE d) { (void)s; (void)c; (void)d; return Qnil; }
static VALUE sprite_update(VALUE s) { (void)s; return Qnil; }

/* ================= Viewport ================= */
typedef struct {
    VALUE rect, color, tone;
    double z, ox, oy;
    int visible;
    int disposed;
} ViewportVal;

static void vp_free(void *p) { free(p); }
static void vp_mark(void *p)
{
    ViewportVal *v = (ViewportVal *)p;
    if (!v) return;
    rb_gc_mark(v->rect); rb_gc_mark(v->color); rb_gc_mark(v->tone);
}
static VALUE vp_alloc(VALUE k) { return Data_Wrap_Struct(k, vp_mark, vp_free, calloc(1, sizeof(ViewportVal))); }
#define GET_VP(v) ((ViewportVal *)DATA_PTR(v))

static VALUE vp_initialize(int argc, VALUE *argv, VALUE self)
{
    VALUE vx, vy, vw, vh;
    ViewportVal *v = GET_VP(self);
    rb_scan_args(argc, argv, "04", &vx, &vy, &vw, &vh);
    VALUE a[4] = { NIL_P(vx) ? INT2FIX(0) : vx, NIL_P(vy) ? INT2FIX(0) : vy,
                   NIL_P(vw) ? INT2FIX(g_screen_w) : vw, NIL_P(vh) ? INT2FIX(g_screen_h) : vh };
    v->rect = rb_class_new_instance(4, a, rgss_rect_class());
    v->visible = 1;
    v->z = 0;
    v->color = Qnil;
    v->tone = Qnil;
    return self;
}

static VALUE vp_rect(VALUE s) { return GET_VP(s)->rect; }
static VALUE vp_rect_set(VALUE s, VALUE v) { GET_VP(s)->rect = v; return v; }
/* viewport 的 color/tone 同样必须始终是对象: 脚本写 @viewport.color.set(...) */
static VALUE vp_color(VALUE s)
{
    ViewportVal *v = GET_VP(s);
    if (NIL_P(v->color)) {
        VALUE a[4] = { INT2FIX(0), INT2FIX(0), INT2FIX(0), INT2FIX(0) };
        v->color = rb_class_new_instance(4, a, rgss_color_class());
    }
    return v->color;
}
static VALUE vp_color_set(VALUE s, VALUE val) { GET_VP(s)->color = val; return val; }
static VALUE vp_tone(VALUE s)
{
    ViewportVal *v = GET_VP(s);
    if (NIL_P(v->tone)) {
        VALUE a[4] = { INT2FIX(0), INT2FIX(0), INT2FIX(0), INT2FIX(0) };
        v->tone = rb_class_new_instance(4, a, rgss_tone_class());
    }
    return v->tone;
}
static VALUE vp_tone_set(VALUE s, VALUE val) { GET_VP(s)->tone = val; return val; }
static VALUE vp_ox(VALUE s) { return rb_float_new(GET_VP(s)->ox); }
static VALUE vp_ox_set(VALUE s, VALUE v) { GET_VP(s)->ox = NUM2DBL(v); return v; }
static VALUE vp_oy(VALUE s) { return rb_float_new(GET_VP(s)->oy); }
static VALUE vp_oy_set(VALUE s, VALUE v) { GET_VP(s)->oy = NUM2DBL(v); return v; }
static VALUE vp_z(VALUE s) { return rb_float_new(GET_VP(s)->z); }
static VALUE vp_z_set(VALUE s, VALUE v) { GET_VP(s)->z = NUM2DBL(v); return v; }
static VALUE vp_visible(VALUE s) { return GET_VP(s)->visible ? Qtrue : Qfalse; }
static VALUE vp_visible_set(VALUE s, VALUE v) { GET_VP(s)->visible = RTEST(v); return v; }
static VALUE vp_dispose(VALUE s) { GET_VP(s)->disposed = 1; return Qnil; }
static VALUE vp_disposed_p(VALUE s) { return GET_VP(s)->disposed ? Qtrue : Qfalse; }
static VALUE vp_flash(VALUE s, VALUE c, VALUE d) { (void)s; (void)c; (void)d; return Qnil; }
static VALUE vp_update(VALUE s) { (void)s; return Qnil; }

/* ================= Plane ================= */
typedef struct {
    VALUE viewport, bitmap, color, tone;
    double x, y, z, ox, oy, zoom_x, zoom_y;
    int visible, opacity, blend_type;
} PlaneVal;
static void plane_free(void *p) { free(p); }
static void plane_mark(void *p)
{
    PlaneVal *v = (PlaneVal *)p;
    if (!v) return;
    rb_gc_mark(v->viewport); rb_gc_mark(v->bitmap);
    rb_gc_mark(v->color); rb_gc_mark(v->tone);
}
static VALUE plane_alloc(VALUE k) { return Data_Wrap_Struct(k, plane_mark, plane_free, calloc(1, sizeof(PlaneVal))); }
#define GET_PLANE(v) ((PlaneVal *)DATA_PTR(v))
static VALUE plane_initialize(int argc, VALUE *argv, VALUE self)
{
    VALUE vp; rb_scan_args(argc, argv, "01", &vp);
    PlaneVal *p = GET_PLANE(self);
    /* calloc 让 VALUE 字段为 0 (Qfalse), 必须显式设 Qnil */
    p->viewport = vp;
    p->bitmap = Qnil;
    p->color  = Qnil;
    p->tone   = Qnil;
    p->visible = 1; p->opacity = 255;
    p->zoom_x = p->zoom_y = 1.0;
    return self;
}
#define PLN_ACC(name, field, conv, rconv) \
    static VALUE plane_##name(VALUE s) { return rconv(GET_PLANE(s)->field); } \
    static VALUE plane_##name##_set(VALUE s, VALUE v) { GET_PLANE(s)->field = conv(v); return v; }
PLN_ACC(x, x, NUM2DBL, rb_float_new)
PLN_ACC(y, y, NUM2DBL, rb_float_new)
PLN_ACC(z, z, NUM2DBL, rb_float_new)
PLN_ACC(ox, ox, NUM2DBL, rb_float_new)
PLN_ACC(oy, oy, NUM2DBL, rb_float_new)
PLN_ACC(zoom_x, zoom_x, NUM2DBL, rb_float_new)
PLN_ACC(zoom_y, zoom_y, NUM2DBL, rb_float_new)
PLN_ACC(opacity, opacity, NUM2INT, INT2FIX)
PLN_ACC(blend_type, blend_type, NUM2INT, INT2FIX)
static VALUE plane_visible(VALUE s) { return GET_PLANE(s)->visible ? Qtrue : Qfalse; }
static VALUE plane_visible_set(VALUE s, VALUE v) { GET_PLANE(s)->visible = RTEST(v); return v; }
static VALUE plane_bitmap(VALUE s) { return GET_PLANE(s)->bitmap; }
static VALUE plane_bitmap_set(VALUE s, VALUE v) { GET_PLANE(s)->bitmap = v; return v; }
static VALUE plane_viewport(VALUE s) { return GET_PLANE(s)->viewport; }
static VALUE plane_viewport_set(VALUE s, VALUE v) { GET_PLANE(s)->viewport = v; return v; }
static VALUE plane_color(VALUE s) { return GET_PLANE(s)->color; }
static VALUE plane_color_set(VALUE s, VALUE v) { GET_PLANE(s)->color = v; return v; }
static VALUE plane_tone(VALUE s) { return GET_PLANE(s)->tone; }
static VALUE plane_tone_set(VALUE s, VALUE v) { GET_PLANE(s)->tone = v; return v; }
static VALUE plane_dispose(VALUE s) { (void)s; return Qnil; }
static VALUE plane_disposed_p(VALUE s) { (void)s; return Qfalse; }
static VALUE plane_update(VALUE s) { (void)s; return Qnil; }

/* ================= Tilemap ================= */
typedef struct {
    VALUE viewport, map_data, flash_data, passages, bitmaps;
    double x, y, z, ox, oy;
    int visible, opacity, blend_type;
} TilemapVal;
static void tilemap_free(void *p) { free(p); }
static void tilemap_mark(void *p)
{
    TilemapVal *t = (TilemapVal *)p;
    if (!t) return;
    rb_gc_mark(t->viewport); rb_gc_mark(t->map_data);
    rb_gc_mark(t->flash_data); rb_gc_mark(t->passages);
    rb_gc_mark(t->bitmaps);
}
static VALUE tilemap_alloc(VALUE k) { return Data_Wrap_Struct(k, tilemap_mark, tilemap_free2, calloc(1, sizeof(TilemapVal))); }
#define GET_TILEMAP(v) ((TilemapVal *)DATA_PTR(v))
static VALUE tilemap_initialize(int argc, VALUE *argv, VALUE self)
{
    VALUE vp; rb_scan_args(argc, argv, "01", &vp);
    TilemapVal *t = GET_TILEMAP(self);
    t->viewport = vp; t->visible = 1; t->opacity = 255;
    /* calloc 给的 0 是 Qfalse 而非 Qnil, 必须显式初始化所有 VALUE 字段 */
    t->map_data = Qnil; t->flash_data = Qnil;
    t->passages = Qnil; t->bitmaps = Qnil;
    tilemap_register(t);
    return self;
}
#define TMP_ACC(name, field, conv, rconv) \
    static VALUE tilemap_##name(VALUE s) { return rconv(GET_TILEMAP(s)->field); } \
    static VALUE tilemap_##name##_set(VALUE s, VALUE v) { GET_TILEMAP(s)->field = conv(v); return v; }
TMP_ACC(x, x, NUM2DBL, rb_float_new)
TMP_ACC(y, y, NUM2DBL, rb_float_new)
TMP_ACC(z, z, NUM2DBL, rb_float_new)
TMP_ACC(ox, ox, NUM2DBL, rb_float_new)
TMP_ACC(oy, oy, NUM2DBL, rb_float_new)
TMP_ACC(opacity, opacity, NUM2INT, INT2FIX)
TMP_ACC(blend_type, blend_type, NUM2INT, INT2FIX)
TMP_ACC(map_data, map_data, (VALUE), (VALUE))
static VALUE tilemap_visible(VALUE s) { return GET_TILEMAP(s)->visible ? Qtrue : Qfalse; }
static VALUE tilemap_visible_set(VALUE s, VALUE v) { GET_TILEMAP(s)->visible = RTEST(v); return v; }
static VALUE tilemap_viewport(VALUE s) { return GET_TILEMAP(s)->viewport; }
static VALUE tilemap_viewport_set(VALUE s, VALUE v) { GET_TILEMAP(s)->viewport = v; return v; }
static VALUE tilemap_dispose(VALUE s) { (void)s; return Qnil; }
static VALUE tilemap_disposed_p(VALUE s) { (void)s; return Qfalse; }
static VALUE tilemap_update(VALUE s) { (void)s; return Qnil; }

/* bitmaps: 9 张图块素材 (TileA1..A5, TileB..E), 脚本按索引写入。
 * 原版是内建 9 元素数组, 索引越界会抛 IndexError; 这里同样用 Ruby Array。 */
static VALUE tilemap_bitmaps(VALUE s)
{
    TilemapVal *t = GET_TILEMAP(s);
    if (NIL_P(t->bitmaps)) {
        /* 预填 9 个 nil, 让 bitmaps[n] = bmp 直接落在范围内 */
        t->bitmaps = rb_ary_new3(9, Qnil, Qnil, Qnil, Qnil, Qnil,
                                 Qnil, Qnil, Qnil, Qnil);
    }
    return t->bitmaps;
}
static VALUE tilemap_bitmaps_set(VALUE s, VALUE v)
{
    GET_TILEMAP(s)->bitmaps = v;
    return v;
}
static VALUE tilemap_passages(VALUE s) { return GET_TILEMAP(s)->passages; }
static VALUE tilemap_passages_set(VALUE s, VALUE v) { GET_TILEMAP(s)->passages = v; return v; }
static VALUE tilemap_flash_data(VALUE s) { return GET_TILEMAP(s)->flash_data; }
static VALUE tilemap_flash_data_set(VALUE s, VALUE v) { GET_TILEMAP(s)->flash_data = v; return v; }

/* ================= Window ================= */
typedef struct {
    VALUE viewport, windowskin, contents, cursor_rect;
    double x, y, z, ox, oy, zoom_x, zoom_y, openness, back_opacity, contents_opacity;
    int visible, pause, active;
    int width, height, opacity;
} WindowVal;
static void tilemap_register(void *tp)
{
    TilemapVal *t = (TilemapVal *)tp;
    for (int i = 0; i < g_tilemap_count; i++) if (g_tilemaps[i] == t) return;
    if (g_tilemap_count < MAX_TILEMAPS) g_tilemaps[g_tilemap_count++] = t;
}
static void tilemap_unregister(void *tp)
{
    for (int i = 0; i < g_tilemap_count; i++)
        if (g_tilemaps[i] == (TilemapVal *)tp) {
            for (int j = i; j < g_tilemap_count - 1; j++) g_tilemaps[j] = g_tilemaps[j+1];
            g_tilemap_count--; return;
        }
}
static void tilemap_free2(void *p) { tilemap_unregister(p); tilemap_free(p); }
static void win_unregister(void *w);
static void window_free(void *p) { if (p) win_unregister(p); free(p); }
static void window_mark(void *p)
{
    WindowVal *w = (WindowVal *)p;
    if (!w) return;
    rb_gc_mark(w->viewport); rb_gc_mark(w->windowskin);
    rb_gc_mark(w->contents); rb_gc_mark(w->cursor_rect);
}
static VALUE window_alloc(VALUE k) { return Data_Wrap_Struct(k, window_mark, window_free, calloc(1, sizeof(WindowVal))); }
#define GET_WIN(v) ((WindowVal *)DATA_PTR(v))
#define MAX_WINDOWS 512
static WindowVal *g_windows[MAX_WINDOWS];
static int g_window_count;

static void win_register(WindowVal *w)
{
    if (g_window_count < MAX_WINDOWS) g_windows[g_window_count++] = w;
}
static void win_unregister(void *wp)
{
    WindowVal *w = (WindowVal *)wp;
    for (int i = 0; i < g_window_count; i++)
        if (g_windows[i] == w) {
            for (int j = i; j < g_window_count - 1; j++) g_windows[j] = g_windows[j+1];
            g_window_count--; return;
        }
}

static VALUE window_initialize(int argc, VALUE *argv, VALUE self)
{
    VALUE vx, vy, vw, vh, vvp;
    WindowVal *w = GET_WIN(self);
    rb_scan_args(argc, argv, "04", &vx, &vy, &vw, &vh, &vvp);
    /* calloc 让 VALUE 字段为 0 (Qfalse), 必须显式设 Qnil */
    w->windowskin = Qnil;
    w->x = NIL_P(vx) ? 0 : NUM2INT(vx);
    w->y = NIL_P(vy) ? 0 : NUM2INT(vy);
    w->viewport = vvp;
    w->visible = 1;
    w->active = 1;   /* calloc 给 0=false, 而 cursor_movable? 要求 active 为真 */
    w->openness = 255;
    w->back_opacity = 192;
    w->opacity = 255;
    w->contents_opacity = 255;
    w->zoom_x = w->zoom_y = 1.0;
    w->width  = NIL_P(vw) ? 0 : NUM2INT(vw);
    w->height = NIL_P(vh) ? 0 : NUM2INT(vh);
    /* contents 是 Bitmap */
    VALUE a[2] = { NIL_P(vw) ? INT2FIX(0) : vw, NIL_P(vh) ? INT2FIX(0) : vh };
    w->contents = rb_class_new_instance(2, a, rgss_bitmap_class());
    VALUE ra[4] = { INT2FIX(0), INT2FIX(0), INT2FIX(0), INT2FIX(0) };
    w->cursor_rect = rb_class_new_instance(4, ra, rgss_rect_class());
    win_register(w);
    {
        FILE *f = fopen("rgss_window.log", "a");
        if (f) { fprintf(f, "[win] created w=%d h=%d x=%.0f y=%.0f count=%d\n",
                         w->width, w->height, w->x, w->y, g_window_count); fclose(f); }
    }
    return self;
}
#define WIN_ACC(name, field, conv, rconv) \
    static VALUE window_##name(VALUE s) { return rconv(GET_WIN(s)->field); } \
    static VALUE window_##name##_set(VALUE s, VALUE v) { GET_WIN(s)->field = conv(v); return v; }
WIN_ACC(x, x, NUM2DBL, rb_float_new)
WIN_ACC(y, y, NUM2DBL, rb_float_new)
WIN_ACC(z, z, NUM2DBL, rb_float_new)
WIN_ACC(ox, ox, NUM2DBL, rb_float_new)
WIN_ACC(oy, oy, NUM2DBL, rb_float_new)
WIN_ACC(zoom_x, zoom_x, NUM2DBL, rb_float_new)
WIN_ACC(zoom_y, zoom_y, NUM2DBL, rb_float_new)
WIN_ACC(back_opacity, back_opacity, NUM2DBL, rb_float_new)
WIN_ACC(contents_opacity, contents_opacity, NUM2DBL, rb_float_new)

/* openness 必须是 0..255 的整数语义:
 * Window_Base#update 用 openness += 48 逐帧推进, 靠 == 255 判断到顶;
 * 若不钳制会一路加到 288 永远等不到 255, 展开动画死循环。 */
static VALUE window_openness(VALUE s)
{
    return INT2FIX((int)GET_WIN(s)->openness);
}
static VALUE window_openness_set(VALUE s, VALUE v)
{
    int n = NUM2INT(v);
    if (n < 0)   n = 0;
    if (n > 255) n = 255;
    GET_WIN(s)->openness = n;
    return INT2FIX(n);
}
/* Window#opacity: 窗体整体不透明度 (0..255) */
static VALUE window_opacity(VALUE s) { return INT2FIX(GET_WIN(s)->opacity); }
static VALUE window_opacity_set(VALUE s, VALUE v)
{
    int n = NUM2INT(v);
    if (n < 0)   n = 0;
    if (n > 255) n = 255;
    GET_WIN(s)->opacity = n;
    return INT2FIX(n);
}
static VALUE window_width(VALUE s) { return INT2FIX(GET_WIN(s)->width); }
static VALUE window_width_set(VALUE s, VALUE v)
{
    GET_WIN(s)->width = NUM2INT(v);
    FILE *f = fopen("rgss_window.log", "a");
    if (f) { fprintf(f, "[win] width=%d\n", GET_WIN(s)->width); fclose(f); }
    return v;
}
static VALUE window_height(VALUE s) { return INT2FIX(GET_WIN(s)->height); }
static VALUE window_height_set(VALUE s, VALUE v)
{
    GET_WIN(s)->height = NUM2INT(v);
    FILE *f = fopen("rgss_window.log", "a");
    if (f) { fprintf(f, "[win] height=%d\n", GET_WIN(s)->height); fclose(f); }
    return v;
}
static VALUE window_visible(VALUE s) { return GET_WIN(s)->visible ? Qtrue : Qfalse; }
static VALUE window_visible_set(VALUE s, VALUE v) { GET_WIN(s)->visible = RTEST(v); return v; }
static VALUE window_pause(VALUE s) { return GET_WIN(s)->pause ? Qtrue : Qfalse; }
static VALUE window_pause_set(VALUE s, VALUE v) { GET_WIN(s)->pause = RTEST(v); return v; }
static VALUE window_active(VALUE s) { return GET_WIN(s)->active ? Qtrue : Qfalse; }
static VALUE window_active_set(VALUE s, VALUE v) { GET_WIN(s)->active = RTEST(v); return v; }
static VALUE window_viewport(VALUE s) { return GET_WIN(s)->viewport; }
static VALUE window_viewport_set(VALUE s, VALUE v) { GET_WIN(s)->viewport = v; return v; }
static VALUE window_contents(VALUE s) { return GET_WIN(s)->contents; }
static VALUE window_contents_set(VALUE s, VALUE v) { GET_WIN(s)->contents = v; return v; }
static VALUE window_windowskin(VALUE s) { return GET_WIN(s)->windowskin; }
static VALUE window_windowskin_set(VALUE s, VALUE v) { GET_WIN(s)->windowskin = v; return v; }
static VALUE window_cursor_rect(VALUE s) { return GET_WIN(s)->cursor_rect; }
static VALUE window_cursor_rect_set(VALUE s, VALUE v) { GET_WIN(s)->cursor_rect = v; return v; }
static VALUE window_dispose(VALUE s) { (void)s; return Qnil; }
static VALUE window_disposed_p(VALUE s) { (void)s; return Qfalse; }
static VALUE window_update(VALUE s) { (void)s; return Qnil; }

/* ================= 注册 ================= */
void rgss_init_graphics(VALUE super)
{
    (void)super;
    mGraphics = rb_define_module("Graphics");
    rb_define_module_function(mGraphics, "update", gfx_update, 0);
    rb_define_module_function(mGraphics, "frame_rate", gfx_frame_rate, 0);
    rb_define_module_function(mGraphics, "frame_rate=", gfx_frame_rate_set, 1);
    rb_define_module_function(mGraphics, "frame_count", gfx_frame_count, 0);
    rb_define_module_function(mGraphics, "frame_count=", gfx_frame_count_set, 1);
    rb_define_module_function(mGraphics, "frame_reset", gfx_frame_reset, 0);
    rb_define_module_function(mGraphics, "brightness", gfx_brightness, 0);
    rb_define_module_function(mGraphics, "brightness=", gfx_brightness_set, 1);
    rb_define_module_function(mGraphics, "freeze", gfx_freeze, 0);
    rb_define_module_function(mGraphics, "transition", gfx_transition, -1);
    rb_define_module_function(mGraphics, "wait", gfx_wait, 1);
    rb_define_module_function(mGraphics, "fadeout", gfx_fadeout, 1);
    rb_define_module_function(mGraphics, "fadein", gfx_fadein, 1);
    rb_define_module_function(mGraphics, "snap_to_bitmap", gfx_snap_to_bitmap, 0);
    rb_define_module_function(mGraphics, "resize_screen", gfx_resize_screen, 2);
    rb_define_module_function(mGraphics, "_reset", gfx_reset, 0);
}

void rgss_init_input(VALUE super)
{
    static const struct { const char *name; int value; } consts[] = {
        { "DOWN",  RGSS_KEY_DOWN  }, { "LEFT",  RGSS_KEY_LEFT  },
        { "RIGHT", RGSS_KEY_RIGHT }, { "UP",    RGSS_KEY_UP    },
        { "A",     RGSS_KEY_A     }, { "B",     RGSS_KEY_B     },
        { "C",     RGSS_KEY_C     }, { "X",     RGSS_KEY_X     },
        { "Y",     RGSS_KEY_Y     }, { "Z",     RGSS_KEY_Z     },
        { "L",     RGSS_KEY_L     }, { "R",     RGSS_KEY_R     },
        { "SHIFT", RGSS_KEY_SHIFT }, { "CTRL",  RGSS_KEY_CTRL  },
        { "ALT",   RGSS_KEY_ALT   },
        { "F5",    RGSS_KEY_F5    }, { "F6",    RGSS_KEY_F6    },
        { "F7",    RGSS_KEY_F7    }, { "F8",    RGSS_KEY_F8    },
        { "F9",    RGSS_KEY_F9    },
        { NULL, 0 }
    };
    int i;
    (void)super;

    mInput = rb_define_module("Input");

    rb_define_module_function(mInput, "update",   input_update,    0);
    rb_define_module_function(mInput, "press?",   input_press_p,   1);
    rb_define_module_function(mInput, "trigger?", input_trigger_p, 1);
    rb_define_module_function(mInput, "repeat?",  input_repeat_p,  1);
    rb_define_module_function(mInput, "dir4",     input_dir4,      0);
    rb_define_module_function(mInput, "dir8",     input_dir8,      0);

    /* 键号常量 — 脚本里 Input::C 之类必须先存在, 否则求值即 NameError */
    for (i = 0; consts[i].name; i++)
        rb_define_const(mInput, consts[i].name, INT2FIX(consts[i].value));
}

void rgss_init_audio(VALUE super)
{
    (void)super;
    mAudio = rb_define_module("Audio");
    rb_define_module_function(mAudio, "bgm_play", audio_play, -1);
    rb_define_module_function(mAudio, "bgm_stop", audio_stop, 0);
    rb_define_module_function(mAudio, "bgm_fade", audio_fade, 1);
    rb_define_module_function(mAudio, "bgs_play", audio_play, -1);
    rb_define_module_function(mAudio, "bgs_stop", audio_stop, 0);
    rb_define_module_function(mAudio, "bgs_fade", audio_fade, 1);
    rb_define_module_function(mAudio, "me_play", audio_play, -1);
    rb_define_module_function(mAudio, "me_stop", audio_stop, 0);
    rb_define_module_function(mAudio, "me_fade", audio_fade, 1);
    rb_define_module_function(mAudio, "se_play", audio_play, -1);
    rb_define_module_function(mAudio, "se_stop", audio_stop, 0);
}

void rgss_init_sprite(VALUE super)
{
    cSprite = rb_define_class("Sprite", super);
    rb_define_alloc_func(cSprite, sprite_alloc);
    rb_define_method(cSprite, "initialize", sprite_initialize, -1);
    rb_define_method(cSprite, "dispose", sprite_dispose, 0);
    rb_define_method(cSprite, "disposed?", sprite_disposed_p, 0);
    rb_define_method(cSprite, "update", sprite_update, 0);
    rb_define_method(cSprite, "flash", sprite_flash, 2);
    rb_define_method(cSprite, "x", sprite_x, 0);
    rb_define_method(cSprite, "x=", sprite_x_set, 1);
    rb_define_method(cSprite, "y", sprite_y, 0);
    rb_define_method(cSprite, "y=", sprite_y_set, 1);
    rb_define_method(cSprite, "z", sprite_z, 0);
    rb_define_method(cSprite, "z=", sprite_z_set, 1);
    rb_define_method(cSprite, "ox", sprite_ox, 0);
    rb_define_method(cSprite, "ox=", sprite_ox_set, 1);
    rb_define_method(cSprite, "oy", sprite_oy, 0);
    rb_define_method(cSprite, "oy=", sprite_oy_set, 1);
    rb_define_method(cSprite, "zoom_x", sprite_zoom_x, 0);
    rb_define_method(cSprite, "zoom_x=", sprite_zoom_x_set, 1);
    rb_define_method(cSprite, "zoom_y", sprite_zoom_y, 0);
    rb_define_method(cSprite, "zoom_y=", sprite_zoom_y_set, 1);
    rb_define_method(cSprite, "angle", sprite_angle, 0);
    rb_define_method(cSprite, "angle=", sprite_angle_set, 1);
    rb_define_method(cSprite, "opacity", sprite_opacity, 0);
    rb_define_method(cSprite, "opacity=", sprite_opacity_set, 1);
    rb_define_method(cSprite, "blend_type", sprite_blend_type, 0);
    rb_define_method(cSprite, "blend_type=", sprite_blend_type_set, 1);
    rb_define_method(cSprite, "visible", sprite_visible, 0);
    rb_define_method(cSprite, "visible=", sprite_visible_set, 1);
    rb_define_method(cSprite, "mirror", sprite_mirror, 0);
    rb_define_method(cSprite, "mirror=", sprite_mirror_set, 1);
    rb_define_method(cSprite, "wave_amp", sprite_wave_amp, 0);
    rb_define_method(cSprite, "wave_amp=", sprite_wave_amp_set, 1);
    rb_define_method(cSprite, "wave_length", sprite_wave_length, 0);
    rb_define_method(cSprite, "wave_length=", sprite_wave_length_set, 1);
    rb_define_method(cSprite, "wave_speed", sprite_wave_speed, 0);
    rb_define_method(cSprite, "wave_speed=", sprite_wave_speed_set, 1);
    rb_define_method(cSprite, "wave_phase", sprite_wave_phase, 0);
    rb_define_method(cSprite, "wave_phase=", sprite_wave_phase_set, 1);
    rb_define_method(cSprite, "bush_depth", sprite_bush_depth, 0);
    rb_define_method(cSprite, "bush_depth=", sprite_bush_depth_set, 1);
    rb_define_method(cSprite, "bush_opacity", sprite_bush_opacity, 0);
    rb_define_method(cSprite, "bush_opacity=", sprite_bush_opacity_set, 1);
    rb_define_method(cSprite, "bitmap", sprite_bitmap, 0);
    rb_define_method(cSprite, "bitmap=", sprite_bitmap_set, 1);
    rb_define_method(cSprite, "src_rect", sprite_src_rect, 0);
    rb_define_method(cSprite, "src_rect=", sprite_src_rect_set, 1);
    rb_define_method(cSprite, "viewport", sprite_viewport, 0);
    rb_define_method(cSprite, "viewport=", sprite_viewport_set, 1);
    rb_define_method(cSprite, "color", sprite_color, 0);
    rb_define_method(cSprite, "color=", sprite_color_set, 1);
    rb_define_method(cSprite, "tone", sprite_tone, 0);
    rb_define_method(cSprite, "tone=", sprite_tone_set, 1);
}

void rgss_init_viewport(VALUE super)
{
    cViewport = rb_define_class("Viewport", super);
    rb_define_alloc_func(cViewport, vp_alloc);
    rb_define_method(cViewport, "initialize", vp_initialize, -1);
    rb_define_method(cViewport, "rect", vp_rect, 0);
    rb_define_method(cViewport, "rect=", vp_rect_set, 1);
    rb_define_method(cViewport, "color", vp_color, 0);
    rb_define_method(cViewport, "color=", vp_color_set, 1);
    rb_define_method(cViewport, "tone", vp_tone, 0);
    rb_define_method(cViewport, "tone=", vp_tone_set, 1);
    rb_define_method(cViewport, "ox", vp_ox, 0);
    rb_define_method(cViewport, "ox=", vp_ox_set, 1);
    rb_define_method(cViewport, "oy", vp_oy, 0);
    rb_define_method(cViewport, "oy=", vp_oy_set, 1);
    rb_define_method(cViewport, "z", vp_z, 0);
    rb_define_method(cViewport, "z=", vp_z_set, 1);
    rb_define_method(cViewport, "visible", vp_visible, 0);
    rb_define_method(cViewport, "visible=", vp_visible_set, 1);
    rb_define_method(cViewport, "flash", vp_flash, 2);
    rb_define_method(cViewport, "dispose", vp_dispose, 0);
    rb_define_method(cViewport, "disposed?", vp_disposed_p, 0);
    rb_define_method(cViewport, "update", vp_update, 0);
}

void rgss_init_plane(VALUE super)
{
    cPlane = rb_define_class("Plane", super);
    rb_define_alloc_func(cPlane, plane_alloc);
    rb_define_method(cPlane, "initialize", plane_initialize, -1);
    rb_define_method(cPlane, "x", plane_x, 0);
    rb_define_method(cPlane, "x=", plane_x_set, 1);
    rb_define_method(cPlane, "y", plane_y, 0);
    rb_define_method(cPlane, "y=", plane_y_set, 1);
    rb_define_method(cPlane, "z", plane_z, 0);
    rb_define_method(cPlane, "z=", plane_z_set, 1);
    rb_define_method(cPlane, "ox", plane_ox, 0);
    rb_define_method(cPlane, "ox=", plane_ox_set, 1);
    rb_define_method(cPlane, "oy", plane_oy, 0);
    rb_define_method(cPlane, "oy=", plane_oy_set, 1);
    rb_define_method(cPlane, "zoom_x", plane_zoom_x, 0);
    rb_define_method(cPlane, "zoom_x=", plane_zoom_x_set, 1);
    rb_define_method(cPlane, "zoom_y", plane_zoom_y, 0);
    rb_define_method(cPlane, "zoom_y=", plane_zoom_y_set, 1);
    rb_define_method(cPlane, "opacity", plane_opacity, 0);
    rb_define_method(cPlane, "opacity=", plane_opacity_set, 1);
    rb_define_method(cPlane, "blend_type", plane_blend_type, 0);
    rb_define_method(cPlane, "blend_type=", plane_blend_type_set, 1);
    rb_define_method(cPlane, "visible", plane_visible, 0);
    rb_define_method(cPlane, "visible=", plane_visible_set, 1);
    rb_define_method(cPlane, "bitmap", plane_bitmap, 0);
    rb_define_method(cPlane, "bitmap=", plane_bitmap_set, 1);
    rb_define_method(cPlane, "viewport", plane_viewport, 0);
    rb_define_method(cPlane, "viewport=", plane_viewport_set, 1);
    rb_define_method(cPlane, "color", plane_color, 0);
    rb_define_method(cPlane, "color=", plane_color_set, 1);
    rb_define_method(cPlane, "tone", plane_tone, 0);
    rb_define_method(cPlane, "tone=", plane_tone_set, 1);
    rb_define_method(cPlane, "dispose", plane_dispose, 0);
    rb_define_method(cPlane, "disposed?", plane_disposed_p, 0);
    rb_define_method(cPlane, "update", plane_update, 0);
}

void rgss_init_tilemap(VALUE super)
{
    cTilemap = rb_define_class("Tilemap", super);
    rb_define_alloc_func(cTilemap, tilemap_alloc);
    rb_define_method(cTilemap, "initialize", tilemap_initialize, -1);
    rb_define_method(cTilemap, "x", tilemap_x, 0);
    rb_define_method(cTilemap, "x=", tilemap_x_set, 1);
    rb_define_method(cTilemap, "y", tilemap_y, 0);
    rb_define_method(cTilemap, "y=", tilemap_y_set, 1);
    rb_define_method(cTilemap, "z", tilemap_z, 0);
    rb_define_method(cTilemap, "z=", tilemap_z_set, 1);
    rb_define_method(cTilemap, "ox", tilemap_ox, 0);
    rb_define_method(cTilemap, "ox=", tilemap_ox_set, 1);
    rb_define_method(cTilemap, "oy", tilemap_oy, 0);
    rb_define_method(cTilemap, "oy=", tilemap_oy_set, 1);
    rb_define_method(cTilemap, "opacity", tilemap_opacity, 0);
    rb_define_method(cTilemap, "opacity=", tilemap_opacity_set, 1);
    rb_define_method(cTilemap, "blend_type", tilemap_blend_type, 0);
    rb_define_method(cTilemap, "blend_type=", tilemap_blend_type_set, 1);
    rb_define_method(cTilemap, "map_data", tilemap_map_data, 0);
    rb_define_method(cTilemap, "map_data=", tilemap_map_data_set, 1);
    rb_define_method(cTilemap, "visible", tilemap_visible, 0);
    rb_define_method(cTilemap, "visible=", tilemap_visible_set, 1);
    rb_define_method(cTilemap, "bitmaps", tilemap_bitmaps, 0);
    rb_define_method(cTilemap, "bitmaps=", tilemap_bitmaps_set, 1);
    rb_define_method(cTilemap, "passages", tilemap_passages, 0);
    rb_define_method(cTilemap, "passages=", tilemap_passages_set, 1);
    rb_define_method(cTilemap, "flash_data", tilemap_flash_data, 0);
    rb_define_method(cTilemap, "flash_data=", tilemap_flash_data_set, 1);
    rb_define_method(cTilemap, "viewport", tilemap_viewport, 0);
    rb_define_method(cTilemap, "viewport=", tilemap_viewport_set, 1);
    rb_define_method(cTilemap, "dispose", tilemap_dispose, 0);
    rb_define_method(cTilemap, "disposed?", tilemap_disposed_p, 0);
    rb_define_method(cTilemap, "update", tilemap_update, 0);
}

void rgss_init_window(VALUE super)
{
    cWindow = rb_define_class("Window", super);
    rb_define_alloc_func(cWindow, window_alloc);
    rb_define_method(cWindow, "initialize", window_initialize, -1);
    rb_define_method(cWindow, "x", window_x, 0);
    rb_define_method(cWindow, "x=", window_x_set, 1);
    rb_define_method(cWindow, "y", window_y, 0);
    rb_define_method(cWindow, "y=", window_y_set, 1);
    rb_define_method(cWindow, "z", window_z, 0);
    rb_define_method(cWindow, "z=", window_z_set, 1);
    rb_define_method(cWindow, "ox", window_ox, 0);
    rb_define_method(cWindow, "ox=", window_ox_set, 1);
    rb_define_method(cWindow, "oy", window_oy, 0);
    rb_define_method(cWindow, "oy=", window_oy_set, 1);
    rb_define_method(cWindow, "zoom_x", window_zoom_x, 0);
    rb_define_method(cWindow, "zoom_x=", window_zoom_x_set, 1);
    rb_define_method(cWindow, "zoom_y", window_zoom_y, 0);
    rb_define_method(cWindow, "zoom_y=", window_zoom_y_set, 1);
    rb_define_method(cWindow, "opacity", window_opacity, 0);
    rb_define_method(cWindow, "opacity=", window_opacity_set, 1);
    rb_define_method(cWindow, "openness", window_openness, 0);
    rb_define_method(cWindow, "openness=", window_openness_set, 1);
    rb_define_method(cWindow, "back_opacity", window_back_opacity, 0);
    rb_define_method(cWindow, "back_opacity=", window_back_opacity_set, 1);
    rb_define_method(cWindow, "contents_opacity", window_contents_opacity, 0);
    rb_define_method(cWindow, "contents_opacity=", window_contents_opacity_set, 1);
    rb_define_method(cWindow, "width", window_width, 0);
    rb_define_method(cWindow, "width=", window_width_set, 1);
    rb_define_method(cWindow, "height", window_height, 0);
    rb_define_method(cWindow, "height=", window_height_set, 1);
    rb_define_method(cWindow, "visible", window_visible, 0);
    rb_define_method(cWindow, "visible=", window_visible_set, 1);
    rb_define_method(cWindow, "pause", window_pause, 0);
    rb_define_method(cWindow, "pause=", window_pause_set, 1);
    rb_define_method(cWindow, "active", window_active, 0);
    rb_define_method(cWindow, "active=", window_active_set, 1);
    rb_define_method(cWindow, "viewport", window_viewport, 0);
    rb_define_method(cWindow, "viewport=", window_viewport_set, 1);
    rb_define_method(cWindow, "contents", window_contents, 0);
    rb_define_method(cWindow, "contents=", window_contents_set, 1);
    rb_define_method(cWindow, "windowskin", window_windowskin, 0);
    rb_define_method(cWindow, "windowskin=", window_windowskin_set, 1);
    rb_define_method(cWindow, "cursor_rect", window_cursor_rect, 0);
    rb_define_method(cWindow, "cursor_rect=", window_cursor_rect_set, 1);
    rb_define_method(cWindow, "dispose", window_dispose, 0);
    rb_define_method(cWindow, "disposed?", window_disposed_p, 0);
    rb_define_method(cWindow, "update", window_update, 0);
}

VALUE rgss_graphics_module(void) { return mGraphics; }
VALUE rgss_input_module(void) { return mInput; }
VALUE rgss_audio_module(void) { return mAudio; }

void rgss_graphics_set_hwnd(HWND h, int w, int hh)
{
    g_hwnd = h;
    g_screen_w = w;
    g_screen_h = hh;
    rgss_render_init(h, w, hh);
}

/* ================= 场景合成 ================= */
/* 所有 Sprite 注册到链表（按 z 排序后绘制） */
#define MAX_SPRITES 2048
static SpriteVal *g_sprites[MAX_SPRITES];
static int g_sprite_count;



void rgss_sprite_register(void *sp)
{
    SpriteVal *s = (SpriteVal *)sp;
    if (g_sprite_count < MAX_SPRITES)
        g_sprites[g_sprite_count++] = s;
}

void rgss_sprite_unregister(void *sp)
{
    SpriteVal *s = (SpriteVal *)sp;
    for (int i = 0; i < g_sprite_count; i++) {
        if (g_sprites[i] == s) {
            for (int j = i; j < g_sprite_count - 1; j++)
                g_sprites[j] = g_sprites[j + 1];
            g_sprite_count--;
            return;
        }
    }
}

/* 按 z 升序（插入排序，元素数少） */
static void sort_sprites(void)
{
    for (int i = 1; i < g_sprite_count; i++) {
        SpriteVal *key = g_sprites[i];
        int j = i - 1;
        while (j >= 0 && g_sprites[j]->z > key->z) {
            g_sprites[j + 1] = g_sprites[j];
            j--;
        }
        g_sprites[j + 1] = key;
    }
}

/* 取 Bitmap 的像素表面 */
static int bitmap_surface(VALUE vbmp, SrcSurface *out)
{
    if (NIL_P(vbmp) || !rb_obj_is_kind_of(vbmp, rgss_bitmap_class())) return 0;
    /* 通过 Ruby 方法取（避免跨文件结构体依赖） */
    VALUE vw = rb_funcall(vbmp, rb_intern("width"), 0);
    VALUE vh = rb_funcall(vbmp, rb_intern("height"), 0);
    int w = NUM2INT(vw), h = NUM2INT(vh);
    if (w <= 0 || h <= 0) return 0;
    out->w = w; out->h = h; out->stride = w;
    /* Bitmap 结构首字段即 pixels（见 rgss_bitmap.c） */
    out->bits = *(uint32_t **)DATA_PTR(vbmp);
    return out->bits != NULL;
}

/* ================= Tilemap 绘制 =================
 * VX 图块 32x32; map_data 为 Table (宽, 高, 层数)。
 * tile_id 编码 (对照原版渲染实测):
 *   0        = 空白
 *   id >= 1  : kind = (id-1)/48, shape = (id-1)%48
 *              kind  0-31 -> TileA2 第 kind    块 (2x3 格式, 64x96)
 *              kind 32-47 -> TileA1 第 kind-32 块 (海水/海滩)
 *              kind 48-79 -> TileA3 第 kind-48 块
 *              kind 80+   -> TileA4 第 kind-80 块
 * 实测锚点: id 2048 -> kind42 -> A1#10 = 深海 (本图 96.76% 的海面)
 *           id 1537 -> kind32 -> A1#0  = 海滩 (沙洲路径)
 * 自动图块 2x3 块含 6 片 32x32; 内部(四邻同) 用自平铺片, 边角用对应角片。
 */
VALUE rgss_tilemap_bitmap(VALUE tilemap, int index);

static void draw_tile_cell(int dx, int dy, VALUE vbmp, int block_x, int block_y,
                           int bw, int bh, int px, int py)
{
    /* 从图块表 vbmp 的 (block_x,block_y) 处取 bw*bh 像素画到 (dx+px, dy+py) */
    SrcSurface s;
    if (!bitmap_surface(vbmp, &s)) return;
    int sw = bw, sh = bh;
    if (block_x + sw > s.w) sw = s.w - block_x;
    if (block_y + sh > s.h) sh = s.h - block_y;
    if (sw <= 0 || sh <= 0) return;
    for (int y = 0; y < sh; y++) {
        int ty = dy + py + y;
        if (ty < 0 || ty >= rgss_render_height()) continue;
        int syy = block_y + y;
        if (syy < 0 || syy >= s.h) continue;
        for (int x = 0; x < sw; x++) {
            int tx = dx + px + x;
            if (tx < 0 || tx >= rgss_render_width()) continue;
            int sxx = block_x + x;
            if (sxx < 0 || sxx >= s.w) continue;
            uint32_t sp = s.bits[(size_t)s.stride * syy + sxx];
            if (((sp >> 24) & 0xFF) == 0) continue;
            uint32_t *dp = (uint32_t *)rgss_render_get_bits()
                           + (size_t)ty * rgss_render_width() + tx;
            *dp = 0xFF000000u | (sp & 0x00FFFFFFu);
        }
    }
}

/* 把一个自动图块单元画到 (dx,dy)。vb = 9 元素 bitmaps 数组 */
static void draw_autotile(int dx, int dy, VALUE vb, int kind, int shape,
                          int up_s, int right_s, int down_s, int left_s)
{
    static const int sheet_index[4] = { 1, 0, 2, 3 };  /* A2,A1,A3,A4 在 bitmaps 里的槽位 */
    int sheet, block, bw, bh;
    if (kind < 32)      { sheet = 0; block = kind;      bw = 64; bh = 96; }
    else if (kind < 48) { sheet = 1; block = kind - 32; bw = 64; bh = 96; }
    else if (kind < 80) { sheet = 2; block = kind - 48; bw = 64; bh = 64; }
    else                { sheet = 3; block = kind - 80; bw = 64; bh = 64; }
    VALUE vsheet = rb_ary_entry(vb, sheet_index[sheet]);
    if (NIL_P(vsheet)) return;

    int bx = (block % 8) * 64, by = (block / 8) * bh;
    /* 2x3 格式 6 片: 0=(0,0) 1=(1,0) 2=(0,1) 3=(1,1) 4=(0,2) 5=(1,2)
     * 实测: 自平铺的内部片多为 (0,0)。四角片用 1..4。 */
    int tl = (up_s && left_s)   ? 0 : 1;
    int tr = (up_s && right_s)  ? 0 : 2;
    int bl = (down_s && left_s) ? 0 : 3;
    int br = (down_s && right_s)? 0 : 4;
    struct { int piece, px, py; } q[4] = {
        { tl, 0, 0 }, { tr, 32, 0 }, { bl, 0, 32 }, { br, 32, 32 }
    };
    for (int i = 0; i < 4; i++) {
        int pc = q[i].piece;
        draw_tile_cell(dx, dy, vsheet,
                       bx + (pc % 2) * 32, by + (pc / 2) * 32, 32, 32,
                       q[i].px, q[i].py);
    }
}

static void draw_tilemap_layer(TilemapVal *t, VALUE vdata, int layer, int ox, int oy)
{
    { static int dbg = 0; if (dbg++ < 2 && f_open_log("rgss_tilemap.log", "[tl] layer=%d W=%d H=%d Z=%d bmpnil=%d tmaps=%d", layer, NUM2INT(rb_funcall(vdata, rb_intern("xsize"),0)), NUM2INT(rb_funcall(vdata, rb_intern("ysize"),0)), NUM2INT(rb_funcall(vdata, rb_intern("zsize"),0)), NIL_P(t->bitmaps), g_tilemap_count)) ; }
    int W = NUM2INT(rb_funcall(vdata, rb_intern("xsize"), 0));
    int H = NUM2INT(rb_funcall(vdata, rb_intern("ysize"), 0));
    int Z = NUM2INT(rb_funcall(vdata, rb_intern("zsize"), 0));
    if (layer >= Z) return;
    VALUE vb = NIL_P(t->bitmaps) ? Qnil : t->bitmaps;
    if (NIL_P(vb)) return;

    int x0 = ox / 32 - 1, y0 = oy / 32 - 1;
    int x1 = x0 + rgss_render_width() / 32 + 3;
    int y1 = y0 + rgss_render_height() / 32 + 3;

    for (int y = y0; y <= y1; y++) {
        if (y < 0 || y >= H) continue;
        for (int x = x0; x <= x1; x++) {
            if (x < 0 || x >= W) continue;
            int id = NUM2INT(rb_funcall(vdata, rb_intern("[]"), 3,
                                        INT2FIX(x), INT2FIX(y), INT2FIX(layer)));
            if (id == 0) continue;
            int dx = x * 32 - ox, dy = y * 32 - oy;
            int kind = (id - 1) / 48, shape = (id - 1) % 48;
            /* 邻接判定: 同层相邻格是否同一自动图块种类 */
            int up = 0, down = 0, left = 0, right = 0;
            if (y > 0)     { int n = NUM2INT(rb_funcall(vdata, rb_intern("[]"), 3, INT2FIX(x), INT2FIX(y-1), INT2FIX(layer))); up    = (n != 0) && ((n-1)/48 == kind); }
            if (y < H - 1) { int n = NUM2INT(rb_funcall(vdata, rb_intern("[]"), 3, INT2FIX(x), INT2FIX(y+1), INT2FIX(layer))); down  = (n != 0) && ((n-1)/48 == kind); }
            if (x > 0)     { int n = NUM2INT(rb_funcall(vdata, rb_intern("[]"), 3, INT2FIX(x-1), INT2FIX(y), INT2FIX(layer))); left  = (n != 0) && ((n-1)/48 == kind); }
            if (x < W - 1) { int n = NUM2INT(rb_funcall(vdata, rb_intern("[]"), 3, INT2FIX(x+1), INT2FIX(y), INT2FIX(layer))); right = (n != 0) && ((n-1)/48 == kind); }
            draw_autotile(dx, dy, vb, kind, shape, up, right, down, left);
        }
    }
}

/* ================= Window 9 宫格皮肤 =================
 * 皮肤 bitmap (Graphics/System/Window.png, 128x128) 布局 (像素级实测):
 *   (0,  0, 64,64) 背景 — 垂直渐变 (64,127,192)->(0,1,128)
 *                    原版窗口 104px 高内部渐变单调无接缝 => 拉伸而非平铺
 *   (64, 0, 64,64) 边框 — 6px 边圈: 黑/白/白/192灰/128灰/黑, 圆角,
 *                    内部 alpha 0 (透出背景), 中心 (17,17)-(47,47) 有箭头
 *                    => 9 宫格切角取 6 即可完全避开箭头
 *   (64,64, 32,32) 光标 (小白框)   (0,64,64,64) 备用暗纹
 * 边框实测对照 (原版截图 x=186..192): 黑,白,白,192,128,黑,然后内部  ✓
 */
#define SKIN_BG_X    0
#define SKIN_BG_Y    0
#define SKIN_BG_SIZE 64
#define SKIN_FRAME_X 64
#define SKIN_FRAME_Y 0
#define SKIN_FRAME_SIZE 64
#define SKIN_BORDER  6       /* 边圈厚度 (实测 6px) */
#define SKIN_CORNER  6       /* 切角 = 边圈厚度, 避开中心箭头 */

/* 从皮肤取 (sx,sy,sw,sh) 拉伸画到 (dx,dy,dw,dh)，alpha 为整体不透明度 */
static void skin_blit(SrcSurface *skin, int sx, int sy, int sw, int sh,
                      int dx, int dy, int dw, int dh, int alpha)
{
    if (!skin->bits || dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0 || alpha <= 0)
        return;
    uint32_t *dst = (uint32_t *)rgss_render_get_bits();
    int rw = rgss_render_width(), rh = rgss_render_height();
    for (int y = 0; y < dh; y++) {
        int ty = dy + y;
        if (ty < 0 || ty >= rh) continue;
        int syy = sy + (int)((long)y * sh / dh);
        if (syy < sy || syy >= sy + sh || syy >= skin->h) continue;
        for (int x = 0; x < dw; x++) {
            int tx = dx + x;
            if (tx < 0 || tx >= rw) continue;
            int sxx = sx + (int)((long)x * sw / dw);
            if (sxx < sx || sxx >= sx + sw || sxx >= skin->w) continue;
            uint32_t sp = skin->bits[(size_t)skin->stride * syy + sxx];
            uint32_t sa = ((sp >> 24) & 0xFF) * (uint32_t)alpha / 255;
            if (sa == 0) continue;
            uint32_t *dp = dst + (size_t)ty * rw + tx;
            uint32_t sr = (sp >> 16) & 0xFF, sg = (sp >> 8) & 0xFF, sb = sp & 0xFF;
            uint32_t dr = (*dp >> 16) & 0xFF, dg = (*dp >> 8) & 0xFF, db = *dp & 0xFF;
            uint32_t ia = 255 - sa;
            *dp = 0xFF000000u
                | (((sr * sa + dr * ia) / 255) << 16)
                | (((sg * sa + dg * ia) / 255) << 8)
                |  ((sb * sa + db * ia) / 255);
        }
    }
}

/* 无皮肤时的兜底: 纯色半透明底（保证窗口始终可见） */
static void draw_window_solid(WindowVal *w, int wx, int wy, int ww, int wh, int op)
{
    if (op <= 0) return;
    uint32_t back = ((uint32_t)op << 24) | 0x202060u;
    uint32_t *dst = (uint32_t *)rgss_render_get_bits();
    int rw = rgss_render_width(), rh = rgss_render_height();
    for (int yy = 0; yy < wh; yy++) {
        int ty = wy + yy;
        if (ty < 0 || ty >= rh) continue;
        for (int xx = 0; xx < ww; xx++) {
            int tx = wx + xx;
            if (tx < 0 || tx >= rw) continue;
            uint32_t *dp = dst + (size_t)ty * rw + tx;
            uint32_t sa = (back >> 24) & 0xFF, ia = 255 - sa;
            uint32_t r = (((back>>16)&0xFF) * sa + ((*dp>>16)&0xFF) * ia) / 255;
            uint32_t g = (((back>>8)&0xFF) * sa + ((*dp>>8)&0xFF) * ia) / 255;
            uint32_t b = ((back&0xFF) * sa + (*dp&0xFF) * ia) / 255;
            *dp = 0xFF000000u | (r<<16) | (g<<8) | b;
        }
    }
}

/* 通用 9 宫格: 源 (fx,fy,fs,fs), 边厚 b, 切角 c, 画到 (dx,dy,dw,dh) */
static void draw_9slice(SrcSurface *skin, int fx, int fy, int fs,
                        int b, int c, int dx, int dy, int dw, int dh, int alpha)
{
    if (dw <= 0 || dh <= 0 || alpha <= 0) return;
    int iw = fs - 2 * c, ih = fs - 2 * c;
    /* 四边 (拉伸) */
    skin_blit(skin, fx + c,      fy,        iw, b, dx + c,      dy,        dw - 2*c, b, alpha);
    skin_blit(skin, fx + c,      fy + fs-b, iw, b, dx + c,      dy + dh-b, dw - 2*c, b, alpha);
    skin_blit(skin, fx,          fy + c,    b, ih, dx,          dy + c,    b, dh - 2*c, alpha);
    skin_blit(skin, fx + fs - b, fy + c,    b, ih, dx + dw - b, dy + c,    b, dh - 2*c, alpha);
    /* 四角 (1:1) */
    skin_blit(skin, fx,         fy,        c, c, dx,          dy,        c, c, alpha);
    skin_blit(skin, fx + fs-c,  fy,        c, c, dx + dw - c, dy,        c, c, alpha);
    skin_blit(skin, fx,         fy + fs-c, c, c, dx,          dy + dh - c, c, c, alpha);
    skin_blit(skin, fx + fs-c,  fy + fs-c, c, c, dx + dw - c, dy + dh - c, c, c, alpha);
}

/* 用 windowskin 画一个完整窗口 (背景拉伸 + 条纹叠加 + 边框 9 宫格 + 光标) */
static void draw_window_skin(WindowVal *w, int wx, int wy, int ww, int wh)
{
    SrcSurface skin;
    if (NIL_P(w->windowskin) || !bitmap_surface(w->windowskin, &skin)) {
        int op = (int)(w->back_opacity * w->openness / 255);
        draw_window_solid(w, wx, wy, ww, wh, op);
        return;
    }
    if (w->openness <= 0) return;
    /* openness 垂直压缩 (向中心) — 对应原版 open/close 动画 */
    int h_eff = (int)(wh * w->openness / 255);
    if (h_eff <= 0) return;
    int y_eff = wy + (wh - h_eff) / 2;

    /* 1) 背景: 整块拉伸 (0,0,64,64), 不透明度 = back_opacity * openness
     *    实测原版内部渐变无 64px 周期 => 拉伸而非平铺 */
    int bop = (int)(w->back_opacity * w->openness / 255);
    if (bop > 255) bop = 255;
    skin_blit(&skin, SKIN_BG_X, SKIN_BG_Y, SKIN_BG_SIZE, SKIN_BG_SIZE,
              wx, y_eff, ww, h_eff, bop);

    /* 1b) 条纹叠加: (0,64,64,64) 是 2px 周期的 (0,0,64,a40) 横纹,
     *     原版内部逐行呈 ±15 蓝通道摆动, 平铺铺上即可 */
    for (int ty = 0; ty < h_eff; ty += SKIN_BG_SIZE) {
        int ch = SKIN_BG_SIZE; if (ty + ch > h_eff) ch = h_eff - ty;
        for (int tx = 0; tx < ww; tx += SKIN_BG_SIZE) {
            int cw = SKIN_BG_SIZE; if (tx + cw > ww) cw = ww - tx;
            skin_blit(&skin, 0, 64, cw, ch, wx + tx, y_eff + ty, cw, ch, bop);
        }
    }

    /* 2) 边框: 源 (64,0,64,64) 6px 边圈, 不透明度 = opacity * openness */
    int fop = (int)(w->opacity * w->openness / 255);
    if (fop > 255) fop = 255;
    if (fop > 0)
        draw_9slice(&skin, SKIN_FRAME_X, SKIN_FRAME_Y, SKIN_FRAME_SIZE,
                    SKIN_BORDER, SKIN_CORNER,
                    wx, y_eff, ww, h_eff, fop);

    /* 3) 光标: 皮肤 (64,64,32,32), 1px 白 + 1px 蓝 + 内部 a32
     *    cursor_rect 是 contents 相对坐标 (item_rect 用 contents.width),
     *    故需加 contents 原点 (+16,+16) */
    if (!NIL_P(w->cursor_rect)) {
        int crx = NUM2INT(rb_funcall(w->cursor_rect, rb_intern("x"), 0));
        int cry = NUM2INT(rb_funcall(w->cursor_rect, rb_intern("y"), 0));
        int crw = NUM2INT(rb_funcall(w->cursor_rect, rb_intern("width"), 0));
        int crh = NUM2INT(rb_funcall(w->cursor_rect, rb_intern("height"), 0));
        if (crw > 0 && crh > 0) {
            /* 闪烁: 原版实测三角波, 周期 40 帧, alpha 128..255
             * (内部亮度 20 级电平、每级 0.686 => alpha 跨度 128, 线性 6.35/帧;
             *  不是硬开关, 光标从不消失只变暗一半) */
            int phase = g_frame_count % 40;
            int blink = (phase < 20) ? (128 + 127 * phase / 20)
                                     : (128 + 127 * (40 - phase) / 20);
            int cop = fop * blink / 255;
            draw_9slice(&skin, 64, 64, 32, 2, 2,
                        wx + 16 + crx, wy + 16 + cry, crw, crh, cop);
        }
    }
}

void rgss_compose_scene(void)
{
    if (!rgss_render_get_bits()) return;
    /* 地图层先画 (位于精灵之下) */
    for (int ti = 0; ti < g_tilemap_count; ti++) {
        TilemapVal *tm = (TilemapVal *)g_tilemaps[ti];
        if (!tm || !tm->visible) continue;
        int tox = (int)tm->ox, toy = (int)tm->oy;
        for (int layer = 0; layer < 2; layer++)
            draw_tilemap_layer(tm, tm->map_data, layer, tox, toy);
    }
    sort_sprites();
    for (int i = 0; i < g_sprite_count; i++) {
        SpriteVal *s = g_sprites[i];
        if (!s || s->disposed || !s->visible) continue;
        if (NIL_P(s->bitmap)) continue;

        SrcSurface src;
        if (!bitmap_surface(s->bitmap, &src)) continue;

        /* src_rect 裁剪 */
        int sx = 0, sy = 0, sw = src.w, sh = src.h;
        if (!NIL_P(s->src_rect)) {
            sx = NUM2INT(rb_funcall(s->src_rect, rb_intern("x"), 0));
            sy = NUM2INT(rb_funcall(s->src_rect, rb_intern("y"), 0));
            int rw = NUM2INT(rb_funcall(s->src_rect, rb_intern("width"), 0));
            int rh = NUM2INT(rb_funcall(s->src_rect, rb_intern("height"), 0));
            if (rw > 0) sw = rw;
            if (rh > 0) sh = rh;
        }

        /* 目标位置与缩放 */
        int dw = (int)(sw * s->zoom_x);
        int dh = (int)(sh * s->zoom_y);
        if (dw <= 0 || dh <= 0) continue;
        int dx = (int)(s->x - s->ox);
        int dy = (int)(s->y - s->oy);

        rgss_render_blit(&src, dx, dy, sx, sy, sw, sh, dw, dh,
                         s->opacity, s->blend_type);
    }

    /* ---- 绘制所有 Window（在原版中是 NxWindow 层） ---- */
    {
        static int s_wlog;
        if (s_wlog < 3) {
            s_wlog++;
            FILE *f = fopen("rgss_window.log", "a");
            if (f) {
                fprintf(f, "[compose] count=%d", g_window_count);
                for (int k = 0; k < g_window_count; k++) {
                    WindowVal *ww = g_windows[k];
                    fprintf(f, " | %d:vis=%d %dx%d skin=%d cont=%d",
                            k, ww ? ww->visible : -1, ww ? ww->width : -1,
                            ww ? ww->height : -1,
                            (ww && !NIL_P(ww->windowskin)) ? 1 : 0,
                            (ww && !NIL_P(ww->contents)) ? 1 : 0);
                }
                fputc(10, f); fclose(f);
            }
        }
    }
    for (int i = 0; i < g_window_count; i++) {
        WindowVal *w = g_windows[i];
        if (!w || !w->visible) continue;
        if (w->width <= 0 || w->height <= 0) continue;
        int wx = (int)w->x, wy = (int)w->y;
        int ww = w->width, wh = w->height;

        /* 1) 背景 + 边框: 用 windowskin 9 宫格绘制 (无皮肤时退回纯色底) */
        draw_window_skin(w, wx, wy, ww, wh);

        /* 2) contents（窗口内绘制的 Bitmap）
         *    Window_Base#create_contents 用 (width-32, height-32),
         *    故原版把它画在窗口内 (+16,+16) 处, 并随 ox/oy 滚动 */
        if (!NIL_P(w->contents)) {
            SrcSurface c;
            if (bitmap_surface(w->contents, &c)) {
                int cw = c.w, ch = c.h;
                if (cw > ww - 32) cw = ww - 32;
                if (ch > wh - 32) ch = wh - 32;
                if (cw > 0 && ch > 0)
                    rgss_render_blit(&c, wx + 16 - (int)w->ox, wy + 16 - (int)w->oy,
                                     0, 0, cw, ch, cw, ch,
                                     (int)(w->contents_opacity * w->openness / 255), 0);
            }
        }
    }
}

long rgss_sprite_count_dbg(void) { return g_sprite_count; }
