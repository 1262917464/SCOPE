# M5-j draw_text 逆向报告（地址标注版）

> 标准：源码逻辑等价——每个原版函数都有对应的还原实现并标注地址，允许已知差异记录在案。
> 本报告先于实现产出，经确认后再动手替换现有 DrawTextW 实现。

## 〇、结论摘要

原版 `draw_text` **完全不用 GDI 画字**（导入表无 TextOut/DrawText/ExtTextOut）。
它把每个字符用 `GetGlyphOutlineW(GGO_GRAY8_BITMAP)` 提取成 8bit 灰度字形，
转成 alpha 图，最后用自家混合器 `0x96680` 合成两遍：阴影（+1,+1 黑色）和正文。

## 一、IAT 实测（tools/iat_probe.c）

之前按"字母序 + 锚点"推的 IAT 全错。进程内实测的真实映射（字体相关）：

| IAT VA | 函数 | 用途 |
|---|---|---|
| `0x100FF020` | gdi32!BitBlt | |
| `0x100FF024` | gdi32!GetGlyphOutlineW | ★ 字形提取 |
| `0x100FF028` | gdi32!EnumFontFamiliesExW | 字体枚举 |
| `0x100FF02C` | gdi32!CreateCompatibleDC | |
| `0x100FF030` | gdi32!CreateFontIndirectW | 字体创建 |
| `0x100FF034` | gdi32!GetTextMetricsW | 度量 |
| `0x100FF038` | gdi32!SelectObject | |
| `0x100FF044` | gdi32!AddFontResourceExW | FR_PRIVATE 注册游戏字体 |
| `0x100FF048` | gdi32!GetDeviceCaps | |
| `0x100FF04C` | gdi32!CreateDIBSection | |

**教训**：dump 的导入表截断在文件末尾（`imp_rva=0x201008`，dump 只有 0x202000），
静态解析不可行；字母序推测也不成立（gdi32 块不按名排序）。必须进程内实测。

## 二、函数地址总表

| 原版地址 | 函数 | 还原实现 | 状态 |
|---|---|---|---|
| `0x9110` | RGSSSetupFonts | `RGSSSetupFonts` (rgss_export.c) | ✅ 已还原 |
| `0x12760` | CFont 构造 | `s_font_default_*` 初始化 | ✅ |
| `0x128A4` | Font#size setter | `font_size_set` | ✅ |
| `0x128C4` | scale setter | —（游戏未改 scale，恒 1.0） | 记录在案 |
| `0x129A0` | 两段式字体创建 | `rgss_create_font` | ✅ 本轮新增 |
| `0x8F060` | 字体名数组过滤 | `font_resolve_face` | ✅ 上轮新增 |
| `0x8EF40` | 字体可用性检查 | （内联于 font_resolve_face） | ✅ 等价 |
| `0x15CC0` | **text_size 度量循环** | 待实现（见 §四） | ⬜ |
| `0x15EB0` | **draw_text 主函数** | 待实现（见 §三） | ⬜ |
| `0x15D64` | text_size 里的 GetGlyphOutlineW | 同上 | ⬜ |
| `0x1602C` | 渲染里 GetGlyphOutlineW（量尺寸） | 同上 | ⬜ |
| `0x160A8` | 渲染里 GetGlyphOutlineW（取位图） | 同上 | ⬜ |
| `0x96680` | 通用 alpha 混合器（25 处调用） | 待实现（text 模式） | ⬜ |

## 三、draw_text = `0x15EB0`（thiscall，7 个栈参数，`ret 0x1c`）

### 3.1 每字符字形提取

```
; —— 尺寸查询（0x1602C）——
push &mat2                 ; MAT2 = 单位矩阵 (eM11.value=1, eM22.value=1, 其余 0)
push 0                     ; lpvBuffer = NULL
push 0                     ; cbBuffer = 0
push &gm                   ; GLYPHMETRICS*
push 6                     ; uFormat = GGO_GRAY8_BITMAP
push uChar                 ; 宽字符
push hdc
call GetGlyphOutlineW      ; 返回所需缓冲区大小

; —— 空字形重试（0x16044-0x16056）——
if (size == 0) { retry++; if (retry < 10) goto 重新查询; }
; 空格特判（0x16032-0x16041）：uChar==0x20 或 0x3000 → size += 1

; —— 分配并取位图（0x16077-0x160A8）——
buf = calloc(size*4 + 4, 1)          ; 0x16077: lea 0x4(ebx,ebx,1)
GetGlyphOutlineW(hdc, uChar, 6, &gm, size, buf, &mat2)   ; 0x160A8
```

### 3.2 灰度 → alpha（`0x160C5-0x16130`）

```c
stride = (gm.gmBlackBoxX + 3) & ~3;          // 0x160D0: lea 3; and ~3
for (y = 0; y < rows; y++) {                  // 0x160F3/0x16130
    for (x = 0; x < gm.gmBlackBoxX; x++) {    // 0x16102/0x16123
        unsigned char g = src[x];             // 0x16106
        if (g == 0x40)      dst[x] = 0xFF;    // 0x1610A: 64 → 255 (完全不透明)
        else if (g > 0)     dst[x] = g * 4;   // 0x16119: add cl,cl ×2 → ×4
    }                                          // 0x1611D
    dst += dst_stride;                        // 0x16127
    src += stride;                            // 0x16129
}
```
GGO_GRAY8_BITMAP 的灰度是 0..64（0=透明，64=不透明），×4 得 0..252，
64 特判为 255（避免 252）。

### 3.3 字形落位

```
dst_x = pen_x + gm.gmptGlyphOrigin.x
dst_y = baseline_y - gm.gmptGlyphOrigin.y
dst_ptr = surface_base + dst_y * surface_stride + dst_x
```
（0x160AE-0x160EF：`ecx+edx` 取 abs 后参与寻址；`imul ebx` 用 surface stride）

### 3.4 步进（`0x16141-0x16170`）

```c
pen_x += gm.gmCellIncX;
// MS Gothic 兼容修正（0x1614D-0x16169，与 text_size 循环 0x15DAB 处重复出现）:
if (font->size == 20 && gm.gmCellIncX == 19 && tm.tmHeight == 20)
    pen_x += 18;   // 0x16169: mov $0x12,%ax  —— 而不是 19
```

### 3.5 阴影与正文合成（`0x1618D-0x16214`）

```c
// 0x1619F: 混合模式 = 0x8e0
// 0x161A9-0x161C2:
CopyRect(&rectShadow, &rectText);
OffsetRect(&rectShadow, 1, 1);            // ← 阴影偏移 (+1,+1) 实测确证
shadowColor = (fontColor.alpha << 24);    // 0x161CA-0x161D5: RGB=0(黑), alpha 用字体的
// 0x161F6: blit(0x96680) —— 阴影遍（条件：[esp+0x104] != 0，即 shadow 开启）
// 0x16214: blit(0x96680) —— 正文遍（用字体颜色）
```
两次调用同一混合器 `0x96680`，仅目标矩形与颜色不同。

## 四、text_size = `0x15CC0`（度量循环）

```
; 0x15D49: GetTextMetricsW(hdc, &tm)          —— 取 tmHeight
; 0x15D64: GetGlyphOutlineW(hdc, ch, 6, &gm, 0, NULL, &mat2)   —— 只量不取
; 0x15D9E: cx = gm.gmCellIncX
; 0x15D93: MS Gothic 修正 (size==20 && cellIncX==19 && tmHeight==20 → 18)
; 0x15DF6: total.width += <cellIncX 与 gmBlackBoxX+origin.x 取较大者>
; 0x15DF8: total.height = max(total.height, tmHeight)
; 0x15D80: 字形查询失败(size==0)时重试，最多 10 次
```
**结论**：`text_size` 也走 GetGlyphOutlineW 逐字符查询，
不是 `GetTextExtentPoint32W`。宽度 = Σ修正后的 cellIncX，高度 = max(tmHeight)。

## 五、CFont 结构（`0x12760` 构造， ecx=this）

| 偏移 | 含义 | 初始值 |
|---|---|---|
| `+0x284` | hFont[]（按名字索引） | 0 |
| `+0x2AC` | ?（hFont 相关） | 0 |
| `+0x2D4` | HDC[]（memdc，按名字索引） | 0 |
| `+0x2FC` | LOGFONTW（92 字节） | memset 0 |
| `+0x313` | LOGFONT.lfCharSet | 1 (DEFAULT_CHARSET) |
| `+0x318` | LOGFONT.lfFaceName（= +0x2FC+0x1C） | — |
| `+0x358` | Font#size → lfHeight | 20（`movl $0x14`） |
| `+0x360` | scale（double） | 1.0（`fld1; fstpl`） |
| `+0x368` | ? | 1 |
| `+0x36C` | 重算标志 byte | 0 |

## 六、实现方案（待主人确认后动手）

1. **`bm_text_size`**：改为逐字符 `GetGlyphOutlineW(GGO_GRAY8_BITMAP, cbBuffer=0)`
   查询 → Σ修正后 cellIncX；高度 = max(tmHeight)。含 10 次重试与空格特判。
2. **`bm_draw_text`**：改为逐字符取 GGO_GRAY8_BITMAP 位图 → 灰度转 alpha
   （64→255，其余 ×4）→ 写入一张 8bit alpha 图 → 最后两遍合成
   （阴影 (+1,+1) 黑色 + 正文用字体颜色）。
3. **旧 DrawTextW 路径**：整段移为 `#if 0` 死代码（按主人决策 1）。
4. **字体创建**：沿用已还原的 `rgss_create_font`（0x129A0 两段式）与
   `font_resolve_face`（0x8F060 过滤）。
5. **已知差异预期**：混合器 `0x96680` 是模式分发的大函数（25 处调用），
   本轮只还原 text 用到的 alpha+纯色 模式；其余模式（Sprite/Window 用）
   不在本次范围。灰阶值本身由 Windows 字体光栅器决定，±1 差异记录在案（决策 2b）。

## 七、实现结果

已实现 `rgss_get_glyph` + `rgss_render_alpha` + `rgss_write_replace`，
替换原 DrawTextW 路径（旧代码 `#if 0` 死代码保留）。

**关键发现 — REPLACE 语义**: 原版 draw_text 的合成是直接写 ARGB（替换），
不是与已有像素混合。Window_Command 的 item 1 被画两次
（refresh 用 alpha=255，draw_item(1,false) 用 alpha=128）。
第二次的 REPLACE 使得第一次的不透明文字/阴影被替换为半透明版本，
禁用项才是灰色而非黑色。

位图的 alpha 字段由窗口合成的 rgss_render_blit 消费
（`sa = (sp >> 24) & 0xFF`），实现 per-pixel alpha 合成。

### 像素验证（禁用项 "讀取存檔"）

| 位置 | 原版 | 重建 |
|---|---|---|
| (210,340) 文字主体 | (146,160,198) | (152,166,210) ✅ |
| (210,344) 阴影边缘 | (67,89,157) | (67,89,157) ✅ 完全一致 |
| (210,341) 阴影内部 | (55,66,104) | (80,101,170) 略偏亮 |

### 已知差异

| # | 差异 | 原因 |
|---|---|---|
| 1 | 阴影内部略偏亮（~25/255） | 原版混合器 0x96680 可能有 alpha 预乘差异 |
| 2 | 抗锯齿风格略异 | DrawTextW 不可比，此处已改 GGO_GRAY8_BITMAP ✓ |
## 七、待实现时确认的点

- `0x15EB0` 的 7 个栈参数与 (x,y,w,h,str,align) 的对应关系
  （从 `ret $0x1c` 反推是 7 个；对齐与裁剪逻辑在函数体内，实现时对照）
- alpha 图的目标 surface 生命周期（每次 draw_text 复用还是新建）
