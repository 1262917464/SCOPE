# M5-f — 数据类实现完成（Color/Tone/Rect/Table/Font）+ 图形类骨架

日期：2026-10-26 ｜ 状态：数据类**已与原版行为一致**；图形类已注册但渲染未接

## 1. 本轮新增源码

| 文件 | 内容 | 状态 |
|------|------|------|
| `src/rgss_data_classes.c` | Color / Tone / Rect / Table / Font 完整实现 | ✅ 行为一致 |
| `src/rgss_bitmap.c` | Bitmap 全 23 方法（含 blt/stretch_blt/draw_text/fill_rect） | ✅ 核心可用 |
| `src/rgss_graphics_classes.c` | Graphics/Sprite/Viewport/Plane/Tilemap/Window/Input/Audio | 🔶 属性完整，渲染待接 |
| `src/rgss_classes.c` | 两阶段注册总控（照抄原版顺序） | ✅ |
| `src/rgss_gamemain.c` | 主循环（窗口/脚本装载/F12 Reset/退出清理） | ✅ 结构完整 |

## 2. 验证结果（与原版实测逐项对比）

| 表达式 | 重建版 | 原版（M5-c 实测） | 一致 |
|--------|--------|------------------|------|
| `Color.new(1,2,3).to_s` | `(1.000000, 2.000000, 3.000000, 255.000000)` | 同 | ✅ |
| `Rect.new(1,2,3,4).to_s` | `(1, 2, 3, 4)` | 同 | ✅ |
| `Tone.new(1,2,3).to_s` | `(1.000000, 2.000000, 3.000000, 0.000000)` | 同（gray 默认 0） | ✅ |
| `Table.new(2,3).xsize` | 2 | 同 | ✅ |
| `t[1,2,0]=99; t[1,2,0]` | 99 | 同 | ✅ |
| `Color.new(1,2,3)==Color.new(1,2,3)` | true | 同 | ✅ |
| `Color.new(1,2,3,4).alpha` | 4.0 | 同（浮点存储） | ✅ |

**阶段 A 类注册**：`defined?(Color/Rect/Tone/Table/Audio)` 全部 `constant` ✅
**阶段 B 延迟**：`Bitmap`/`Graphics` 在 `RGSSInitialize2` 后确实不存在（与原版一致）✅

## 3. 关键实现细节

### Ruby 1.8.1 C API 陷阱（踩坑记录）

1. **vararg 方法签名**：`rb_define_method(..., -1)` 的函数必须是
   `VALUE f(int argc, VALUE *argv, VALUE self)`，**不是** `(VALUE self, int argc, VALUE *argv)`。
   写错会直接段错误。
2. **Data_Wrap_Struct**：`(klass, mark, free, data)` 四参；free 签名 `void (*)(void*)`。
3. **`uint32_t` 需显式 `#include <stdint.h>`**：Ruby 1.8.1 头文件不含。
4. **`RGSSGetStringUTF8` 静态缓冲**：连续调用会互相覆盖（原版行为相同），
   测试时必须每次调用后立即使用。

### Bitmap 实现要点（按 M5-c 规格）

```c
typedef struct {
    uint32_t    *pixels;   /* +0x0C 位置语义: ARGB8888 缓冲 */
    BitmapImpl  *impl;     /* +0x10: → buf{width,height} */
    int          stride;   /* +0x18 */
    int          disposed;
} Bitmap;
/* 索引: pixels[(stride>>2)*y + x]; 越界静默返回 (0,0,0,0) */
```

`draw_text` 用 GDI `DrawTextA` 渲染到临时 DIB 再拷入，字体走 `Font` 属性
（name/size/bold/italic），与 Nx 后端路线一致。

## 4. 当前可运行状态

- `RGSSInitialize2(0)` → Ruby 初始化 + 5 个数据类注册 ✅
- `RGSSEval` / `RGSSGetInt` / `RGSSGetStringUTF8` → 全部工作 ✅
- `RGSSGameMain` → 建窗口 + 读 Game.ini + 装载脚本 + 执行（结构完整）
- 游戏实测：因 `Graphics` 渲染未接 + RPG 内置类缺失，脚本执行会在早期失败

## 5. 下一步（M5-g）

1. **内嵌 RPG 类源码注入**：把 `docs/assets/embedded_rpg_classes.rb` 编进 DLL 并 eval
2. **Graphics 渲染管线**：后缓冲 Bitmap → `Graphics.update` → BitBlt 到窗口（骨架已写，待接 Sprite 绘制）
3. **Sprite/Viewport 实际绘制**：按 z 序合成到后缓冲
4. **图片加载**：BMP/PNG/JPEG 解码（`Bitmap.new(path)` 形式）
5. **RTP 路径与注册表读取**
