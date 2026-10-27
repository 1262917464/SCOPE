# M5-c 续 — Bitmap 内存布局与像素格式（实测解码）
> 注：本文写于 2026-10-27 仓库重构前；文中 `backup/` `unpacked/` `build/` `test/` `third_party/` 等路径现位于 `local/` 下，`src/rgss_*.c` 现为 `src/runtimes/rgss2/*.c`，`tools/` 已按流水线分桶（unpack/pe/analysis/probe/verify）。


日期：2026-10-26 ｜ 方法：`get_pixel` (0x10004490) / `set_pixel` (0x10004550) 汇编逐条解码

## 1. Bitmap 对象布局（C++ / Ruby Data 对象）

```
VALUE (Bitmap 对象, RData)
  +0x00  RBasic (flags, klass)
  +0x0C  DWORD*  pixels_base        ← 像素缓冲基址
  +0x10  BitmapImpl* impl
             +0x08  PixelBuffer* buf
                        +0x04  width   (int)
                        +0x08  height  (int)
  +0x18  int     stride             ← 行跨距（字节）
```

注意 `+0x10` 是**二级间接**（`self->impl->buf` 取 width/height），而像素基址与 stride
直接挂在 Ruby 对象上（`+0x0C` / `+0x18`），这是 Enterbrain 为性能做的扁平化。

## 2. 像素格式：**ARGB8888（32bpp），非 premultiplied**

反编译 `get_pixel` 尾段（0x1000451C-0x1000452E）：
```
px = pixels_base[(stride/4) * y + x]      ; stride 先 +3 再 >>2（向上取整到 dword）
alpha = (px >> 24) & 0xFF
red   = (px >> 16) & 0xFF
green = (px >>  8) & 0xFF
blue  =  px        & 0xFF
```
→ 标准 `0xAARRGGBB`，与 `Color.new(1,2,3)` 实测输出 `(1.000000, 2.000000, 3.000000, 255.000000)`
（alpha 默认 255，浮点存储）完全吻合。

## 3. 坐标检查逻辑（越界行为）

```
if (x < 0 || y < 0) → 返回默认 (0,0,0,0)
if (x >= buf->width || y >= buf->height) → 返回默认
```
即越界**静默返回黑色透明**，不抛异常。重建必须照抄这个行为。

## 4. `disposed?` / 空缓冲处理

```
impl = self[0x10]; buf = impl[0x8]
if (!buf) → rb_raise(..., "disposed bitmap")   ; 字符串 @0x10103DF0
```
确认 **"disposed bitmap" 是 dispose 后访问的报错文案**（M4 字符串区已见）。

## 5. Bitmap 全部方法地址（328 表节选）

| 方法 | fn | argc |
|------|-----|------|
| initialize | 0x10003FA0 | -1 |
| initialize_copy | 0x10004230 | 1 |
| width / height / rect | 0x100043C0 / 0x10004400 / 0x10004440 | 0 |
| get_pixel / set_pixel | 0x10004490 / 0x10004550 | 2 / 3 |
| blt | 0x10004CE0 | -1 |
| stretch_blt | 0x10004E20 | -1 |
| fill_rect / gradient_fill_rect | 0x10004F70 / 0x10005180 | -1 |
| clear / clear_rect | 0x100054F0 / 0x10005540 | 0 / -1 |
| hue_change | 0x10005660 | 1 |
| blur / radial_blur | 0x100056F0 / 0x10005730 | 0 / 2 |
| draw_text | 0x100057B0 | -1 |
| font / font= | 0x10003C90 / 0x10003CD0 | 0 / 1 |
| text_size | 0x10003E20 | 1 |
| dispose / disposed? | 0x10004CA0 / 0x10011420 | 0 |

## 6. draw_text 调用链（0x100057B0）

```
0x100DC7A0  — 参数解包辅助
0x10026860  — 类型检查（"disposed bitmap" 同款断言，arg2=0x22=T_DATA）
0x10025F80  — 取 ivar / 类方法（0x10103DF0="disposed bitmap"）
0x10058A80  — ★ 文本布局核心（返回行/尺寸信息）
0x10008F20  — 字符串/数组工具 ×4
0x100803F0  — rb_str_new / RSTRING 构造 ×2
0x100808D0  — RSTRING_PTR
0x100567D0  — NUM2INT ×5
0x1008BF70  — 读 Font 属性（0x10103E00="size", 0x10103E10="color"）
0x10008BA0  — 属性取值辅助
0x100128C0  — ★ 实际字形绘制
0x10015C70  — 绘制后处理
```

关键：**`draw_text` 先经 `0x10058A80` 做布局（含换行、对齐、行高），
再对每行调 `0x100128C0` 画字**。汉化场景要改文本管线，改这两个函数即可。

## 7. 重建要点（可直接落码）

```c
typedef struct { int width, height; } PixelBuffer;
typedef struct { PixelBuffer *buf; } BitmapImpl;
typedef struct { /* RBasic */ ...; uint32_t *pixels; BitmapImpl *impl; int stride; } Bitmap;
/* get_pixel */
if (x<0||y<0||x>=impl->buf->width||y>=impl->buf->height) return Color(0,0,0,0);
uint32_t px = pixels[(stride>>2)*y + x];
return Color((px>>16)&0xFF, (px>>8)&0xFF, px&0xFF, (px>>24)&0xFF);
```
