/*
 * classes.c — RGSS 类注册（两阶段链，结构照抄原版）
 *
 * 阶段 A (RGSSInitialize2 → sub_10009E60): Audio / Table / Color / Tone / Rect
 * 阶段 B (CGameFrame::Run → sub_10009F40):  RGSSError + Graphics/Bitmap/Viewport/
 *                                          Sprite/Tilemap/Plane/Window/Font/Input
 * 依据: docs/M5-c_图形类注册链.md
 */
#include <windows.h>
#include "ruby.h"

void rgss_init_bitmap(VALUE super);
void rgss_init_color(VALUE super);
void rgss_init_rect(VALUE super);
void rgss_init_tone(VALUE super);
void rgss_init_table(VALUE super);
void rgss_init_font(VALUE super);
void rgss_init_graphics(VALUE super);
void rgss_init_sprite(VALUE super);
void rgss_init_viewport(VALUE super);
void rgss_init_tilemap(VALUE super);
void rgss_init_plane(VALUE super);
void rgss_init_window(VALUE super);
void rgss_init_input(VALUE super);
void rgss_init_audio(VALUE super);

void rgss_set_color_class(VALUE c);
void rgss_set_rect_class(VALUE r);

VALUE rgss_bitmap_class(void);
VALUE rgss_color_class(void);
VALUE rgss_rect_class(void);
VALUE rgss_font_class(void);
VALUE rgss_graphics_module(void);
VALUE rgss_audio_module(void);
VALUE rgss_input_module(void);

static int s_phase_a_done, s_phase_b_done;

/* ---------- 阶段 A: 数据类（RGSSInitialize2 内） ---------- */
void rgss_register_phase_a(void)
{
    if (s_phase_a_done) return;
    s_phase_a_done = 1;
    VALUE obj = rb_cObject;
    /* 顺序照原版 sub_10009E60: Audio → Table → Color → Tone → Rect */
    rgss_init_audio(obj);
    rgss_init_table(obj);
    rgss_init_color(obj);
    rgss_init_tone(obj);
    rgss_init_rect(obj);
    /* 供 Bitmap 等使用 */
    rgss_set_color_class(rgss_color_class());
    rgss_set_rect_class(rgss_rect_class());
    /* load_data / save_data 全局函数（原版 sub_10009E60: rb_define_global_function） */
    {
        extern VALUE rgss_load_data(VALUE self, VALUE fname);
        extern VALUE rgss_save_data(int argc, VALUE *argv, VALUE self);
        rb_define_global_function("load_data", rgss_load_data, 1);
        rb_define_global_function("save_data", rgss_save_data, -1);
    }
    /* Zlib 模块（脚本体 zlib 压缩，必需） */
    { extern void Init_zlib(void); Init_zlib(); }
    /* 内嵌 RPG 类源码（原版 sub_10009E60 末尾 eval 的 blob） */
    {
        extern int rgss_eval_embedded_rpg(void);
        rgss_eval_embedded_rpg();
    }
}

/* ---------- 阶段 B: 图形类（CGameFrame::Run 首步） ---------- */
static void plog(const char *m)
{
    FILE *f = fopen("rgss_phase.log", "a");
    if (f) { fputs(m, f); fputc(10, f); fclose(f); }
}

void rgss_register_phase_b(void)
{
    if (s_phase_b_done) return;
    s_phase_b_done = 1;
    plog("[phaseB] begin");
    VALUE obj = rb_cObject;
    /* RGSSError 先建 */
    VALUE cErr = rb_define_class("RGSSError", rb_eStandardError);
    (void)cErr;
    /* 顺序照原版 sub_10009F40: Graphics → Bitmap → Viewport → Sprite →
       Tilemap → Plane → Window → Font → Input */
    plog("[phaseB] rgss_init_graphics"); rgss_init_graphics(obj);
    plog("[phaseB] rgss_init_bitmap"); rgss_init_bitmap(obj);
    plog("[phaseB] rgss_init_viewport"); rgss_init_viewport(obj);
    plog("[phaseB] rgss_init_sprite"); rgss_init_sprite(obj);
    plog("[phaseB] rgss_init_tilemap"); rgss_init_tilemap(obj);
    plog("[phaseB] rgss_init_plane"); rgss_init_plane(obj);
    plog("[phaseB] rgss_init_window"); rgss_init_window(obj);
    plog("[phaseB] rgss_init_font"); rgss_init_font(obj);
    plog("[phaseB] rgss_init_input"); rgss_init_input(obj);
}
