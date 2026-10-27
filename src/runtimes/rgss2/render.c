/*
 * render.c — Graphics 渲染管线（GDI + DIB 后缓冲 + Sprite 合成）
 *
 * 架构（对应原版 Nx 层）:
 *   后缓冲 DIB (ARGB8888, 544x416) ← Sprite/Plane/Tilemap/Window 绘制
 *        ↓ Graphics.update
 *   BitBlt → 窗口 DC
 *
 * 对应原版: NxScreen/NxWindow/NxCustomDraw32（M2 模块地图）
 */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include "ruby.h"
#include "debug.h"

extern VALUE rgss_bitmap_class(void);
extern VALUE rgss_rect_class(void);

/* 后缓冲（由 graphics_classes.c 的 Graphics 模块共享） */
typedef struct {
    HWND      hwnd;
    int       width, height;
    HDC       memdc;
    HBITMAP   membm;
    uint32_t *bits;
    HDC       dc;
} RenderCtx;

static RenderCtx g_rc;

/* ---------- 初始化/销毁 ---------- */
int rgss_render_init(HWND hwnd, int w, int h)
{
    if (g_rc.bits) rgss_render_shutdown();
    g_rc.hwnd = hwnd;
    g_rc.width = w;
    g_rc.height = h;
    g_rc.dc = GetDC(hwnd);
    g_rc.memdc = CreateCompatibleDC(g_rc.dc);

    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof bi);
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;          /* top-down，与 Bitmap 一致 */
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    g_rc.membm = CreateDIBSection(g_rc.memdc, &bi, DIB_RGB_COLORS,
                                  (void **)&g_rc.bits, NULL, 0);
    if (!g_rc.membm || !g_rc.bits) return 0;
    SelectObject(g_rc.memdc, g_rc.membm);
    memset(g_rc.bits, 0, (size_t)w * h * 4);
    return 1;
}

void rgss_render_shutdown(void)
{
    if (g_rc.membm) { DeleteObject(g_rc.membm); g_rc.membm = NULL; }
    if (g_rc.memdc) { DeleteDC(g_rc.memdc); g_rc.memdc = NULL; }
    if (g_rc.dc) { ReleaseDC(g_rc.hwnd, g_rc.dc); g_rc.dc = NULL; }
    g_rc.bits = NULL;
}

/* ---------- 呈现 ---------- */
static int s_present_count;
void rgss_render_present(void)
{
    if (!g_rc.dc || !g_rc.memdc) return;
    BitBlt(g_rc.dc, 0, 0, g_rc.width, g_rc.height, g_rc.memdc, 0, 0, SRCCOPY);
    s_present_count++;
    if (s_present_count <= 3) {
        FILE *f = RGSS_LOG_FOPEN("rgss_render.log");
        if (f) {
            fprintf(f, "[present] #%d dc=%p memdc=%p %dx%d bits=%p\n",
                    s_present_count, (void*)g_rc.dc, (void*)g_rc.memdc,
                    g_rc.width, g_rc.height, (void*)g_rc.bits);
            fclose(f);
        }
    }
}

/* ---------- 清屏 ---------- */
void rgss_render_clear(uint32_t argb)
{
    if (!g_rc.bits) return;
    size_t n = (size_t)g_rc.width * g_rc.height;
    for (size_t i = 0; i < n; i++) g_rc.bits[i] = argb;
}

/* ---------- 位块传输（带 alpha 混合、缩放、裁剪） ---------- */
typedef struct {
    uint32_t *bits;
    int w, h, stride;
} SrcSurface;

void rgss_render_blit(const SrcSurface *src,
                      int dst_x, int dst_y,
                      int src_x, int src_y, int src_w, int src_h,
                      int dst_w, int dst_h,
                      int opacity, int blend_type)
{
    if (!g_rc.bits || !src || !src->bits) return;
    if (src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) return;
    if (opacity <= 0) return;

    for (int y = 0; y < dst_h; y++) {
        int ty = dst_y + y;
        if (ty < 0 || ty >= g_rc.height) continue;
        int sy = src_y + (int)((long)y * src_h / dst_h);
        if (sy < 0 || sy >= src->h) continue;

        for (int x = 0; x < dst_w; x++) {
            int tx = dst_x + x;
            if (tx < 0 || tx >= g_rc.width) continue;
            int sx = src_x + (int)((long)x * src_w / dst_w);
            if (sx < 0 || sx >= src->w) continue;

            uint32_t sp = src->bits[(size_t)src->stride * sy + sx];
            uint32_t sa = (sp >> 24) & 0xFF;
            if (sa == 0) continue;
            if (opacity < 255) sa = sa * (uint32_t)opacity / 255;
            if (sa == 0) continue;

            uint32_t sr = (sp >> 16) & 0xFF, sg = (sp >> 8) & 0xFF, sb = sp & 0xFF;
            uint32_t *dp = &g_rc.bits[(size_t)ty * g_rc.width + tx];

            switch (blend_type) {
            case 0:   /* 普通 alpha 混合 */
            default: {
                uint32_t dr = (*dp >> 16) & 0xFF, dg = (*dp >> 8) & 0xFF, db = *dp & 0xFF;
                uint32_t ia = 255 - sa;
                uint32_t r = (sr * sa + dr * ia) / 255;
                uint32_t g = (sg * sa + dg * ia) / 255;
                uint32_t b = (sb * sa + db * ia) / 255;
                *dp = 0xFF000000u | (r << 16) | (g << 8) | b;
                break;
            }
            case 3:   /* 预乘 over (仅窗口 contents 合成内部使用)
                       * draw_text 的混合器 (0x1009FE00) 对透明 dst 产出
                       * rgb = src×f / a = f —— 即预乘形式; 原版的 contents
                       * →屏幕合成不再乘 a: screen = src_rgb + dst×(1−a)。
                       * 直通 alpha 合成会把 f 再乘一遍 (f²), 文字边缘变瘦。
                       * 注意 src_rgb + dst×(1−a) 可以超过 255 (如 fa=128 的
                       * 预乘 rgb=128f 与 bg 残余项相加), 必须钳制, 否则通道
                       * 溢出污染相邻字节 (实测禁用项变黄)。 */
            {
                uint32_t dr = (*dp >> 16) & 0xFF, dg = (*dp >> 8) & 0xFF, db = *dp & 0xFF;
                uint32_t ia = 255 - sa;
                uint32_t r = sr + dr * ia / 255; if (r > 255) r = 255;
                uint32_t g = sg + dg * ia / 255; if (g > 255) g = 255;
                uint32_t b = sb + db * ia / 255; if (b > 255) b = 255;
                *dp = 0xFF000000u | (r << 16) | (g << 8) | b;
                break;
            }
            case 1:   /* 加法 */
            {
                uint32_t dr = (*dp >> 16) & 0xFF, dg = (*dp >> 8) & 0xFF, db = *dp & 0xFF;
                uint32_t r = dr + sr * sa / 255; if (r > 255) r = 255;
                uint32_t g = dg + sg * sa / 255; if (g > 255) g = 255;
                uint32_t b = db + sb * sa / 255; if (b > 255) b = 255;
                *dp = 0xFF000000u | (r << 16) | (g << 8) | b;
                break;
            }
            case 2:   /* 减法 */
            {
                uint32_t dr = (*dp >> 16) & 0xFF, dg = (*dp >> 8) & 0xFF, db = *dp & 0xFF;
                uint32_t r = dr - sr * sa / 255;
                uint32_t g = dg - sg * sa / 255;
                uint32_t b = db - sb * sa / 255;
                *dp = 0xFF000000u | ((r > 0 ? r : 0) << 16) | ((g > 0 ? g : 0) << 8) | (b > 0 ? b : 0);
                break;
            }
            }
        }
    }
}

/* ---------- Ruby 侧: Graphics 内部（由 graphics_classes.c 调用） ---------- */
void *rgss_render_get_bits(void) { return g_rc.bits; }
int rgss_render_width(void) { return g_rc.width; }
int rgss_render_height(void) { return g_rc.height; }
