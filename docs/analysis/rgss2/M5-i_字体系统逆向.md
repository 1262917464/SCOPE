# M5-i 字体系统逆向与还原（地址标注版）
> 注：本文写于 2026-10-27 仓库重构前；文中 `backup/` `unpacked/` `build/` `test/` `third_party/` 等路径现位于 `local/` 下，`src/rgss_*.c` 现为 `src/runtimes/rgss2/*.c`，`tools/` 已按流水线分桶（unpack/pe/analysis/probe/verify）。


> 标准：源码逻辑等价 —— 每个原版函数都有对应的还原实现并标注地址，允许已知差异记录在案。

## 一、原版 IAT 实测（tools/iat_probe.c 进程内反查）

之前按"字母序 + 锚点"推 IAT 槽位是**错的**。用 `tools/iat_probe.c`
加载原版 DLL（ASPack 自解压）后直接读 IAT 并用模块导出表反查，得到真实映射：

| IAT VA | 真实函数 | 说明 |
|--------|---------|------|
| `0x100FF020` | gdi32!BitBlt | |
| `0x100FF024` | gdi32!GetGlyphOutlineW | ★ 字形提取（无 TextOut/DrawText 导入！） |
| `0x100FF028` | gdi32!EnumFontFamiliesExW | ★ 字体可用性枚举 |
| `0x100FF02C` | gdi32!CreateCompatibleDC | |
| `0x100FF030` | gdi32!CreateFontIndirectW | ★ 字体创建 |
| `0x100FF034` | gdi32!GetTextMetricsW | ★ 两段式度量的关键 |
| `0x100FF038` | gdi32!SelectObject | |
| `0x100FF044` | gdi32!AddFontResourceExW | FR_PRIVATE 注册游戏字体 |
| `0x100FF048` | gdi32!GetDeviceCaps | |
| `0x100FF04C` | gdi32!CreateDIBSection | |

**重要教训**：dump 的导入表被截断（`imp_rva=0x201008` 已在文件末尾），
静态解析不可行；字母序推测也不成立（gdi32 块不是按名排序）。
必须在进程内实测。

## 二、关键函数地址与还原对照

### 1. `RGSSSetupFonts` → 原版 `sub_10009110`
| 项 | 原版 | 还原 |
|---|---|---|
| 逻辑 | 拼 `<路径>\Fonts\*` → `FindFirstFileW` 枚举 → 每个文件 `AddFontResourceExW(p, FR_PRIVATE, 0)` | 同 |
| 证据 | `0x9255` 处 `push 0; push 0x10; push path; call` → `0x10 = FR_PRIVATE` | `src/rgss_export.c` |
| 差异 | 原版用调用方传入的宽字符串路径；还原用 `GetModuleFileNameA(NULL)` 取 Game.exe 目录（因为不能安全解引用调用方参数） | 已记录 |

### 2. `CFont` 构造 → 原版 `0x12760`
| 字段 | 偏移 | 初始值 |
|---|---|---|
| scale (double) | `+0x360` | 1.0（`fld1; fstpl 0x360`） |
| lfHeight (size) | `+0x358` | 20（`movl $0x14`） |
| LOGFONT | `+0x2FC` | memset 0，92 字节（0x5C = sizeof(LOGFONTW)） |
| lfCharSet | `+0x313` (= LOGFONT+0x17) | 1 = DEFAULT_CHARSET |

→ 还原：`s_font_default_*` + `rgss_create_font`

### 3. size / scale setter → 原版 `0x128A4` / `0x128C4`
- size setter：`cmp %eax,0x358(%ecx); mov %eax,0x358(%ecx)`
- scale setter：`fcoml 0x360(%ecx); fstpl 0x360(%ecx)`
- 游戏全程未改 scale → 恒 1.0

### 4. **字体可用性过滤 → 原版 `0x8F060`**（关键！）
```c
// 0x8F060(list_out, name_array)
out->count = 0;
if (!arr) return;
for (i = 0; i < arr->len; i++) {
    name = arr->ptr[i];
    if (check_font_0x8ef40(name) == 0) {          // 0 = 可用
        if (!already_in(out, name) && out->count < 0x40)  // 上限 64
            out->names[out->count++] = name;
    }
}
```
→ 还原：`font_resolve_face()`（CreateFontW + GetTextFace 回读比对）
**效果**：跳过解析不到的名字。本游戏的数组：
```
["霞鹜文楷等宽 TC Bold",      ← 带样式后缀, 不是合法 family 名 → 跳过
 "霞鹜文楷 屏幕阅读版 R",      ← 可用 → 命中
 "LXGW WenKai Mono TC", ...]
```

### 5. 可用性检查 → 原版 `0x8EF40`
```
0x8ef7a: call 0xfc2f0(name, 0xFFFF, 0x1008, &out1, &out2)
0x8ef7f: cmp eax, -1 → 失败
0x8ef89: cmp eax(err), 0x2736 / 0x276d → 特定错误视为可用
```
（0xfc2f0 是内部封装，疑似 GetGlyphOutlineW/GetGlyphIndicesW 探测字形是否齐全）

### 6. **两段式字体创建 → 原版 `0x129A0`**（这是 size≠像素高的真正原因）
```
0x129AA: eax = index * 64            ; 每个字体名一个 64 字节条目
0x129B1: cmpw 0, [esi+eax+4]         ; 检查名字是否为空
0x129DD: cmpl 0, 0x284(esi+edi*4)    ; hFont 缓存命中?
0x129EB: GetDC(0)                    ; 0x100ff3b0
0x129F7: CreateCompatibleDC(hdc)     ; 0x100FF02C → 缓存于 +0x2D4
0x12A0D: lstrcpynW(&lfFaceName, name, 0x20)   ; LOGFONT+0x1C = +0x318
0x12A2A: lfHeight = [esi+0x358]      ; = Font#size, 直接用, 无换算!
0x12A2D: lfWidth = 0
0x12A37: CreateFontIndirectW(&lf)    ; 第一遍
0x12A78: GetTextMetricsW(memdc, &tm) ; 量 tmHeight / tmAveCharWidth
0x12A9E: lfWidth = (int)(lfHeight * scale * tmAveCharWidth / tmHeight)
0x12ABB: CreateFontIndirectW(&lf)    ; 第二遍
```
**为什么原版 22 号字显示成 16px**：
`lfWidth = round(22 × 1.0 × tmAveCharWidth / tmHeight)`
霞鹜文楷屏幕阅读版的 tmAveCharWidth/tmHeight ≈ 0.73 → 22×0.73 ≈ 16。
GDI 在 lfWidth 非零时按"平均字符宽"缩放字体 → 实际字宽 16px。
→ 还原：`rgss_create_font()`（`src/rgss_bitmap.c`）

### 7. 字形光栅化 → 原版 `0x15D64 / 0x1602C / 0x160A8`
原版**没有导入 TextOut/DrawText/ExtTextOut**，而是用
`GetGlyphOutlineW` 取字形位图后自行光栅化。
→ 还原：目前用 `DrawTextW`（**已知差异，见下**）

## 三、已知差异（记录在案）

| # | 差异 | 原因 | 影响 |
|---|---|---|---|
| 1 | 字形光栅化用 `DrawTextW`，原版用 `GetGlyphOutlineW` 自绘 | 尚未还原 GetGlyphOutlineW 路径 | 抗锯齿风格略异（原版更柔，重建更锐） |
| 2 | `RGSSSetupFonts` 用 exe 目录而非调用方参数 | 无法安全解引用调用方参数 | 无（路径等价） |
| 3 | shadow 用 DrawTextW 第二遍渲染 | 同 #1 | 阴影为 1px 偏移，与原版一致 |

## 四、验证结果

原版 vs 重建（标题命令窗口第一项"新的劇情"）：

| 指标 | 原版 | 重建 |
|---|---|---|
| 文字 bbox | (210,308)-(271,324) w=61 h=16 | (210,308)-(273,324) w=63 h=16 |
| 起点 | (210,308) | (210,308) ✅ 完全一致 |
| 字高 | 16 | 16 ✅ |
| 宽度 | 61 | 63（+2px 抗锯齿差） |

对比图：`docs/assets/window_9slice_compare.png`

## 五、新增工具

| 工具 | 用途 |
|---|---|
| `tools/iat_probe.c` | 进程内读原版 DLL 的 IAT 并反查真实函数名（之前 IAT 猜错的教训） |
| `tools/blinkprobe.c` | 定时截屏测光标闪烁波形 |
| `tools/cursortest.c` | 验证光标移动与闪烁 |
