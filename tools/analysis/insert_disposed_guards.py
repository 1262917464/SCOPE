# insert_disposed_guards.py — B19 一次性补丁: 为五类+Bitmap 的全部访问器/方法
# 插入 disposed 检查 (原版 49+19 处内联 rb_raise 的等价实现)。
# 幂等: 已含 rgss_raise_disposed 的函数跳过。
import re, io

G = 'src/runtimes/rgss2/graphics_classes.c'
B = 'src/runtimes/rgss2/bitmap.c'

def patch_gc(src):
    n = 0
    # 1. extern 声明
    if 'rgss_raise_disposed' not in src:
        src = src.replace('#include "debug.h"',
                          '#include "debug.h"\nvoid rgss_raise_disposed(const char *msg);', 1)
        n += 1
    # 2. 宏守卫 (SPR_ACC / WIN_ACC)
    old_spr = 'static VALUE sprite_##name(VALUE s) { return rconv(GET_SPRITE(s)->field); } \\\n    static VALUE sprite_##name##_set(VALUE s, VALUE v) { GET_SPRITE(s)->field = conv(v); return v; }'
    new_spr = 'static VALUE sprite_##name(VALUE s) { if (GET_SPRITE(s)->disposed) rgss_raise_disposed("disposed sprite"); return rconv(GET_SPRITE(s)->field); } \\\n    static VALUE sprite_##name##_set(VALUE s, VALUE v) { if (GET_SPRITE(s)->disposed) rgss_raise_disposed("disposed sprite"); GET_SPRITE(s)->field = conv(v); return v; }'
    if old_spr in src:
        src = src.replace(old_spr, new_spr, 1); n += 1
    old_win = 'static VALUE window_##name(VALUE s) { return rconv(GET_WIN(s)->field); } \\\n    static VALUE window_##name##_set(VALUE s, VALUE v) { GET_WIN(s)->field = conv(v); return v; }'
    new_win = 'static VALUE window_##name(VALUE s) { if (GET_WIN(s)->disposed) rgss_raise_disposed("disposed window"); return rconv(GET_WIN(s)->field); } \\\n    static VALUE window_##name##_set(VALUE s, VALUE v) { if (GET_WIN(s)->disposed) rgss_raise_disposed("disposed window"); GET_WIN(s)->field = conv(v); return v; }'
    if old_win in src:
        src = src.replace(old_win, new_win, 1); n += 1
    # 3. 显式函数守卫
    classes = [
        ('vp_',      'GET_VP',      'disposed viewport',
         ['vp_rect_set','vp_color_set','vp_tone','vp_tone_set','vp_ox','vp_ox_set','vp_oy','vp_oy_set',
          'vp_z','vp_z_set','vp_visible','vp_visible_set','vp_flash','vp_update']),
        ('plane_',   'GET_PLANE',   'disposed plane',
         ['plane_x','plane_x_set','plane_y','plane_y_set','plane_z','plane_z_set','plane_ox','plane_ox_set',
          'plane_oy','plane_oy_set','plane_zoom_x','plane_zoom_x_set','plane_zoom_y','plane_zoom_y_set',
          'plane_opacity_set','plane_blend_type','plane_blend_type_set','plane_visible','plane_visible_set',
          'plane_bitmap','plane_bitmap_set','plane_viewport','plane_viewport_set','plane_color_set',
          'plane_tone','plane_tone_set','plane_update']),
        ('tilemap_', 'GET_TILEMAP', 'disposed tilemap',
         ['tilemap_x','tilemap_x_set','tilemap_y','tilemap_y_set','tilemap_z','tilemap_z_set',
          'tilemap_ox','tilemap_ox_set','tilemap_oy','tilemap_oy_set','tilemap_opacity_set',
          'tilemap_blend_type','tilemap_blend_type_set','tilemap_map_data','tilemap_map_data_set',
          'tilemap_visible','tilemap_visible_set','tilemap_bitmaps','tilemap_bitmaps_set',
          'tilemap_passages','tilemap_passages_set','tilemap_flash_data','tilemap_flash_data_set',
          'tilemap_viewport','tilemap_viewport_set','tilemap_update']),
        ('window_',  'GET_WIN',     'disposed window',
         ['window_cursor_rect','window_contents','window_width','window_width_set','window_height',
          'window_height_set','window_visible','window_visible_set','window_pause','window_pause_set',
          'window_active_set','window_viewport','window_viewport_set','window_contents_set',
          'window_windowskin','window_windowskin_set','window_cursor_rect_set','window_update',
          'window_openness','window_openness_set','window_opacity_set','window_back_opacity_set',
          'window_contents_opacity_set']),
        ('sprite_',  'GET_SPRITE',  'disposed sprite',
         ['sprite_update','sprite_flash','sprite_bitmap','sprite_bitmap_set','sprite_src_rect_set',
          'sprite_viewport','sprite_viewport_set','sprite_color_set','sprite_tone','sprite_tone_set']),
    ]
    for prefix, get, msg, fns in classes:
        for fn in fns:
            m = re.search(r'static VALUE %s\s*\(([^)]*)\)\s*\{' % re.escape(fn), src)
            if not m:
                continue  # 宏生成或不存在
            body_start = m.end()
            if 'rgss_raise_disposed' in src[body_start:body_start+300]:
                continue  # 幂等
            pm = re.search(r'VALUE\s+(s|self)\b', m.group(1))
            param = pm.group(1) if pm else 's'
            guard = '\n    if (%s(%s)->disposed) rgss_raise_disposed("%s");' % (get, param, msg)
            src = src[:body_start] + guard + src[body_start:]
            n += 1
    return src, n

def patch_bm(src):
    n = 0
    if 'rgss_raise_disposed' not in src:
        src = src.replace('#include "debug.h"',
                          '#include "debug.h"\nvoid rgss_raise_disposed(const char *msg);', 1)
        n += 1
    fns = ['bm_width','bm_height','bm_rect','bm_get_pixel','bm_set_pixel','bm_clear',
           'bm_clear_rect','bm_fill_rect','bm_gradient_fill_rect','bm_blt','bm_stretch_blt',
           'bm_draw_text','bm_text_size','bm_font_get','bm_font_set','bm_hue_change',
           'bm_blur','bm_radial_blur']
    for fn in fns:
        m = re.search(r'static VALUE %s\s*\(([^)]*)\)\s*\{' % re.escape(fn), src)
        if not m:
            print('  !! %s 未找到' % fn); continue
        body_start = m.end()
        if 'rgss_raise_disposed' in src[body_start:body_start+300]:
            continue
        pm = re.search(r'VALUE\s+(s|self)\b', m.group(1))
        param = pm.group(1) if pm else 'self'
        guard = '\n    if (GET_BITMAP(%s)->disposed) rgss_raise_disposed("disposed bitmap");' % param
        src = src[:body_start] + guard + src[body_start:]
        n += 1
    return src, n

src = io.open(G, encoding='utf-8').read()
src, n1 = patch_gc(src)
io.open(G, 'w', encoding='utf-8', newline='').write(src)
print('graphics_classes.c: %d 处插入' % n1)

src = io.open(B, encoding='utf-8').read()
src, n2 = patch_bm(src)
io.open(B, 'w', encoding='utf-8', newline='').write(src)
print('bitmap.c: %d 处插入' % n2)
