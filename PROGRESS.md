# 进度看板 — RGSS202E.dll 逆向重建

> 最后更新：2026-10-27（M5-k 完成：字体创建与字体名选择按原版反汇编修正，文字粗细与原生一致）
> 约定：每完成一个里程碑，更新本文件并在 docs/ 出报告。

## 一、里程碑总览

| 里程碑 | 内容 | 状态 |
|--------|------|------|
| M1 | 脱壳（ASPack）+ 资产清单 + 工具链验证 | ✅ 完成 |
| M1.5 | 可运行的脱壳 DLL（OEP 冻结 dump + IAT 重建） | ✅ 完成 |
| M2 | C++ 类枚举（45 vftable）+ 模块地图 + Game.exe 接口文档 | ✅ 完成 |
| M3 | Ruby 1.8.1 官方源码 mingw32 集成 | ✅ 完成 |
| M4 | 对象层主干：注册链 + Run 骨架 + API 全表 + 内嵌源码 | ✅ 完成 |
| M5-a | 主循环全解 + Init 归属 | ✅ 完成 |
| M5-b | 14 类 Init 全定位 + 328 条 Ruby API 映射表 | ✅ 完成 |
| M5-c | 两阶段注册链闭环 + Bitmap 布局/ARGB8888 格式 | ✅ 完成 |
| M5-d | Scripts.rvdata 全解（95 段 / 613KB / 1227 def） | ✅ 完成 |
| M5-e | 重建版 DLL 骨架（69 导出 1:1 + Ruby 层 7/7 对拍） | ✅ 完成 |
| M5-f | 数据类实现（Color/Tone/Rect/Table/Font 行为一致） | ✅ 完成 |
| **M5-g** | **★标题画面 + 命令窗口 + 中文文字成功渲染★** | ✅ 完成 |
| **M5-h** | **输入系统完整重建 + 场景切换验证 + Tilemap 通路**（音频/皮肤/精确图块表剩余） | ✅ 主体完成 |
| **M5-i** | **字体系统逆向（IAT 实测 + 两段式字体创建 + 地址标注）** | ✅ 完成 |
| **M5-j** | **★draw_text 改 GetGlyphOutlineW 自绘字形 + REPLACE 语义 + per-pixel alpha★** | ✅ 完成 |
| **M5-k** | **★字体创建与字体名选择按反汇编修正 — 文字粗细与原生一致★** | ✅ 完成 |
| M6 | 自动对照测试 + 研究文档 + 地址映射表 | ⬜ 未开始 |

## 二、当前状态（一句话）

**重建版 DLL 已能从标题画面一路进入游戏地图：按 Enter 触发新游戏 → Scene_Map，
地图图块与角色精灵全部渲染，按键可操作。文字使用 GetGlyphOutlineW 自绘字形光栅化
（与原版完全相同的路径），阴影/禁用项/per-pixel alpha 均按 REPLACE 语义还原。**

已验证链路：
1. 标题画面（背景 + 命令窗 + 中文字体 + 阴影 + 光标闪烁）✅
2. `Input.trigger?(Input::C)` 采到 Enter → `command_new_game` ✅
3. `Scene_Title#main` 的 `open_command_window` 动画循环正常退出 ✅
4. `Spriteset_Map#create_tilemap` → `Tilemap#bitmaps[0..8]` 写入成功 ✅
5. 地图 100x100x3 Table 正确装载，海面/地形绘制，角色精灵 78 个 ✅
6. 禁用项半透明灰色（per-pixel alpha + REPLACE 语义）✅

验证截图：`test/Project3_rebuild/kt_00_title.png`（标题）、
`test/Project3_rebuild/kt_01_after_enter.png`（地图），
对比图 `docs/assets/window_9slice_compare.png`。
原版基准：`test/Project3_orig/kt_01.png`。

## 三、产物清单

### 构建产物
| 文件 | 说明 |
|------|------|
| `build/RGSS202E_rebuild.dll` | **3.56 MB**，基址 0x10000000，69 导出与原版 100% 同名 |
| `build/ruby-build/libmsvcrt-ruby18-static.a` | Ruby 1.8.1 静态库（725 符号） |
| `build/rpg_src.o` | objcopy 打包的内嵌 RPG 源码（18KB） |
| `build/zlib_ext.o` | Ruby zlib 扩展 |
| `unpacked/RGSS202E_runnable.dll` | 脱壳可运行版（M1.5 产物，独立可用） |

### 源码树（src/）
| 文件 | 内容 |
|------|------|
| `rgss_export.c` | 30 个核心导出（生命周期/数据桥/字符串/RTP） |
| `rgss_regex_stubs.c` | 39 个正则/编码导出 |
| `rgss_classes.c` | 两阶段注册总控（照抄原版顺序） |
| `rgss_data_classes.c` | Color/Tone/Rect/Table/Font 完整实现 |
| `rgss_bitmap.c` | Bitmap 23 方法（含 blt/stretch_blt/draw_text/UTF-8 文字） |
| `rgss_graphics_classes.c` | Graphics/Sprite/Viewport/Plane/Tilemap/Window/Input/Audio |
| `rgss_render.c` | 渲染管线（后缓冲 DIB + Sprite/Window 合成 + BitBlt） |
| `rgss_gamemain.c` | 主循环（窗口/脚本装载/F12 Reset/退出清理） |
| `rgss_marshal.c` | load_data/save_data |
| `rgss_embedded.c` | 内嵌 RPG 源码注入 |
| `rgss_image.cpp` | GDI+ 图片加载（BMP/PNG/JPEG/GIF + 扩展名回退） |
| `RGSS202E.def` | 69 导出定义（从原版 exports.txt 自动生成） |
| `ext/Win32API/Win32API.c` | 重建版 Win32API（修复原版调用约定炸弹） |

### 分析资产（docs/assets/）
| 文件 | 内容 |
|------|------|
| `rgss_ruby_api_map.tsv` | **328 条 Ruby API → C 实现地址**（14 类全覆盖） |
| `embedded_rpg_classes.rb` | DLL 内嵌 18KB RPG 类源码原文 |
| `Project3_scripts/` | 解压的 95 个游戏脚本（613KB Ruby 源码） |
| `screenshot_with_window.png` | ★标题画面 + 命令窗口 + 中文★ |
| `screenshot_title.png` | 同上（副本） |

### 工具（tools/）
脱壳器 `unpacker3.exe`（调试器+硬件断点）、`dumpat.py`、`mkpe.py`/`mkpe2.py`、
`rtti_scan.py`、`decode_rbdefs.py`、`find_init_fns.py`、`parse_rvdata.py`、
`gen_def.py`、`api_usage*.py`、`impcheck.exe`、`smoke.exe`、`rgss_introspect*.c`、
`ruby_mingw_patches.py`、`fix_idalib_cfg.py`

> 注：`tools/RGSS1_*`、`tools/delphi_probe.py` 属于并行会话的 RGSS1 项目，与本项目无关。

## 四、关键事实（后续直接引用）

### 二进制与脱壳
| 事实 | 证据 |
|------|------|
| OEP = RVA **0xE6E94** | stub 尾部 `push OEP; ret`（M1.5 §1） |
| 原始导入 259 个 / 16 组描述符，IAT 在 RVA 0xFF000–0xFF470 | mkpe2 扫描（M1.5 §2） |
| 壳：ASPack（`.aspack`/`.adata` 或 `.vxdata`），基址 0x10000000 | 节表 |
| apphelp 槽：0xFF2DC=**MessageBoxW**、0xFF324=**IsWindowVisible**(85%) | M2 附录 |
| MSVC 编译 + **完整 RTTI**（45 vftable 可枚举） | Hex-Rays（M1 §5） |

### 架构
| 事实 | 证据 |
|------|------|
| **Ruby 1.8.1**（MRI），用 **GNU regex（非 Oniguruma）** | 版本串 + regex.h 宏（M5-e §4 修正） |
| 三层：**Rx 对象层 → NxDrawLocal 渲染库 / CSx 音频**，CGameFrame 主控 | 45 vftable（M2 §1-2） |
| 图形 = **纯 GDI + DIB**（无 DirectX）；音频 = DirectSound(COM)+MCI/ACM+Ogg | 导入表 |
| ws2_32 = Ruby 线程调度器 select 依赖，非网络功能 | 调用点聚类（M2 §4） |
| 第三方：zlib **1.2.3** / libpng **1.2.16** / JPEG"2.0.8" / Ogg 内置 | 版本串 |

### 初始化与主循环
| 事实 | 证据 |
|------|------|
| **两阶段注册**：RGSSInitialize2 → 5 个数据类（Audio/Table/Color/Tone/Rect）；CGameFrame::Run 首步 → 9 个图形类 | 运行时反射 + objdump（M5-c） |
| 主循环 `sub_1000AFD0`：F12 = **Reset 异常**重启；逐段 eval 脚本 | 全函数反编译（M5-a） |
| `$RGSS_SCRIPTS` = `[id, name, zlib_data]`，运行时 `s[3,0]` 填入解压源码 | 磁盘解析 + 反编译双重验证（M5-d） |
| 运行标志 @0x10173478；Game 单例 @0x1016EFE4；$TEST @0x1016EFE0；CSxAudio @0x1016EFE8 | 反编译 |
| CGameFrame 0x26C 字节，默认 frame_rate=60 | 构造函数 |

### Game.exe 调用契约（★最终确认）
```
DllMain(ATTACH)
RGSSSetupRTP()                     ← 无参数
RGSSSetupFonts(buf, 0x400, ...)    ← 3 参数；返回非 0 才继续
RGSSInitialize2(HWND)              ← 宿主窗口句柄（IsWindow=1，非 HMODULE）
RGSSEval("$TEST = false")
RGSSEval("$BTEST = false")
RGSSGameMain(a, b, c)              ← a 也是宿主窗口句柄
```
证据：探针 DLL 实测 + Game.exe 反汇编 0x4018AE–0x4019B0（M5-g §3）

### Bitmap 内存布局（重建实现依据）
```c
struct Bitmap { uint32_t *pixels; BitmapImpl *impl; int stride; int disposed; };
struct BitmapImpl { PixelBuffer *buf; };            // buf->width, buf->height
// 像素: ARGB8888 (0xAARRGGBB)，索引 pixels[(stride>>2)*y + x]
// 越界静默返回 (0,0,0,0)
```

## 五、已解决的关键缺陷（踩坑记录）

| # | 问题 | 症状 | 修复 |
|---|------|------|------|
| 1 | `sprite_initialize` 签名 | **黑屏秒退** | vararg 必须是 `(int argc, VALUE *argv, VALUE self)` |
| 2 | `RGSSSetupRTP` 签名 | 栈失衡 | 实为**无参数** |
| 3 | `RGSSSetupFonts` 签名 | 栈失衡 | 实为 **3 参数** |
| 4 | 缺 `load_data`/`save_data` | undefined method | `rb_define_global_function` + File.open/Marshal |
| 5 | 缺 Zlib 模块 | uninitialized constant | 编译 ext/zlib + `Init_zlib()` |
| 6 | `Sprite.new` 强制 1 参 | wrong number of args | 改 vararg + `rb_scan_args("01")` |
| 7 | `Graphics.freeze` 阻塞 update | 无帧渲染 | 只做快照标记，不阻塞 |
| 8 | Window 缺 `width=`/`height=` | undefined method | 补属性 |
| 9 | `Bitmap#font` 返回 nil | nil 崩溃 | 首次访问自动创建 Font 实例 |
| 10 | `draw_text` 只认 5 参 | wrong number of args | 增加 `draw_text(rect, str)` 形式 |
| 11 | 图片路径无扩展名 | 加载失败 | GDI+ 自动尝试 .png/.jpg/.bmp/.gif |
| 12 | **compose 不画 Window** | **命令窗口不可见** | 加 Window 注册表 + 合成层 |
| 13 | **draw_text 用 DrawTextA** | **中文乱码** | UTF-8 → UTF-16 → `DrawTextW` |
| 14 | **Input 常量从未注册** | `Input::C` 即 NameError | 补 `rb_define_const` 20 项（原版值实测） |
| 15 | **Input 无键码映射** | 按键永远无效 | 按原版 0x138a0/0x13940 重建状态机 |
| 16 | **`Window#openness=` 未钳制** | `open_command_window` 死循环 | 钳制到 0..255（原版语义） |
| 17 | **calloc 得到 Qfalse** | `x.nil?` 为假、`bitmaps[0]=` 崩 | 显式把 VALUE 字段置 Qnil |
| 18 | **Sprite/Viewport/Font 缺省对象** | `src_rect.set` → nil | getter 懒创建 Rect/Color/Tone |
| 19 | **缺 Tilemap#bitmaps/passages** | Scene_Map 报错 | 补 9 元素数组 + 两属性 |
| 20 | **缺 Viewport#ox/oy** | update_viewports 报错 | 补属性 |
| 21 | **缺 Window#opacity** | Window_NumberInput 报错 | 补属性（0..255 钳制） |
| 22 | **draw_text 拒绝非字符串** | 金钱数字 TypeError | 非 T_STRING 时调 `to_s` |
| 23 | **缺 `Graphics.frame_count=`** | command_new_game 报错 | 补 setter |
| 24 | **★★ Data_Wrap_Struct 无 mark 函数** | **GC 回收后堆损坏 → st_lookup 崩溃** | 为全部类补 `rb_gc_mark` |
| 25 | **★★ Table `_dump`/`_load` 头错** | 地图数据整体错位 | 原版实测头为 `[维数][维1][维2][维3][总数]` |
| 26 | **draw_text 写死白色** | Font#color 全部失效 | 读 Font#color(red/green/blue/alpha) |
| 27 | **无文字阴影** | 与原版观感差 | Font#shadow 为真时黑字偏移 (1,1) 叠加 |
| 28 | **阴影覆盖度恒 0** | 阴影不可见 | 阴影层须渲**白**字取覆盖度再写黑（黑字 RGB 全 0） |
| 29 | **Font.new 不读默认值** | 字体名/字号错 | 改为从 Font.default_* 拷贝 |
| 30 | **RGSSSetupFonts 是空壳** | 游戏自带 kaiu.ttf 未注册 | 原版实测: FindFirstFileW 枚举 `<路径>\Fonts\*` + `AddFontResourceExW(p, FR_PRIVATE, 0)` |
| 31 | **CreateFontA 收 UTF-8 字节** | 字体名永远匹配不到 | 改 CreateFontW + UTF-8→UTF-16 |
| 32 | **size 直接当像素高** | 文字比原版大 30% | 原版按点数换算: `-size*72/dpi` (22→16px, 命中宋体位图字号) |
| 33 | **draw_text 用 DT_TOP** | 文字位置偏高 2px | RGSS 语义是矩形内垂直居中 (DT_VCENTER\|DT_SINGLELINE) |
| 34 | **Window#active 默认 false** | cursor_movable? 恒假, 光标不能动 | calloc 给 0, 需显式 active=1 |
| 35 | **光标不闪烁** | 与原版观感差 | 原版实测: 三角波, 周期 40 帧, alpha 128..255 (从不消失) |
| 36 | **无帧率限制** | 7000fps, 闪烁等帧同步效果失真 | QueryPerformanceCounter 按 frame_rate 限帧 |
| 37 | **draw_text 用 DrawTextW** | 原版无 TextOut/DrawText 导入, 完全不同路径 | GetGlyphOutlineW 自绘字形光栅化 (M5-j) |
| 38 | **合成用 BLEND 而非 REPLACE** | 双重绘制后禁用项变纯黑 | 改为直接写 ARGB (REPLACE 语义), 位图 alpha 由窗口合成消费 |
| 39 | **text_size 用 GetTextExtentPoint32W** | 原版用 GGO 逐字符查询 | 改为逐字符 GetGlyphOutlineW 查询 (Σ修正后 cellIncX) |
| 40 | **阴影 alpha 用 fa** | 禁用项阴影过暗/过亮 | 原版阴影色 = (字体alpha << 24), 即 alpha 随字体色; 按 REPLACE 语义写入 |
| 41 | **字体 lfWidth 无条件做"两段式修正"** | **文字笔画明显偏粗 (白像素 +25.8%)** | 原版 0x12A69 `jnp` 在缩放==1.0 时跳到 0x12ADA 直接返回, lfWidth 恒为 0; 游戏全程不改缩放 → 原版从不做修正。去掉修正后白像素比降到 1.024 |
| 42 | **lfQuality 设成 ANTIALIASED_QUALITY** | 与原版不同的光栅化路径 | 原版 CFont+0x316 从未被写入, 保持 0 = DEFAULT_QUALITY |
| 43 | **font_resolve_face 用 GetTextFaceW 回读匹配 (臆造)** | 判定语义与原版完全不同, 误接受/误拒绝字体名 | 原版真实逻辑: 0x13450 用 EnumFontFamiliesExW 建**系统字体族名集合**(存 RGSS+0x260), setter 用 0x12FE0 查集合, 命中才 add_name (0x12860, 上限 10 个); 0x129A0 按索引逐个 CreateFontIndirectW, 失败才 +1 重试 |
| 44 | **误把 0x8F060/0x8EF40 当字体名过滤器** | 注释与实现都建立在错误前提上 | 0x8EF40 实为**防盗版联网校验** (winsock 错误码 0x2736/0x276D), 与字体无关; 已在 M5-k 更正 |
| 45 | **★字体族集合容器只有 40 槽, 枚举出 5570 个族 → 截断★** | **字体实际回退成宋体 (req≠actual), 字形完全不对** | 容器扩到 16384。截断导致查不到的名字被判"可用" (fail-open), 直接交给 GDI 后静默回退宋体。修正后 req=actual=霞鹜文楷屏幕阅读版R, cellX=17 与原版字距一致 |

### Ruby 1.8.1 C API 陷阱（写代码必看）
1. **vararg 签名**：`rb_define_method(..., -1)` 必须是 `VALUE f(int argc, VALUE *argv, VALUE self)`。
   写成 `(VALUE self, int argc, ...)` 直接段错误 —— 这是黑屏根因。
2. `Data_Wrap_Struct(klass, mark, free, data)` 四参；free 为 `void (*)(void*)`。
3. `uint32_t` 需显式 `#include <stdint.h>`（Ruby 1.8 头不含）。
4. `RB_TYPE_P`/`RSTRING_PTR` 是 1.9+ 命名 → 用 `TYPE(v) == T_STRING` / `RSTRING(v)->ptr`。
5. `rb_mMarshal` 不存在 → `rb_const_get(rb_cObject, rb_intern("Marshal"))`。
6. `Marshal.load` 接受**字符串数据或 IO**，不是文件名。
7. `RGSSGetStringUTF8` 用静态缓冲，连续调用互相覆盖（原版同样行为）。

## 六、构建命令（完整可复现）

```bash
export PATH="/c/msys64/mingw32/bin:$PATH"
RB=build/ruby-build

# C 源
for f in rgss_export rgss_classes rgss_bitmap rgss_data_classes \
         rgss_graphics_classes rgss_gamemain rgss_regex_stubs \
         rgss_render rgss_embedded rgss_marshal; do
  gcc -c -O2 -o build/$f.o -I$RB -I$RB/win32 src/$f.c
done
# C++ 源（GDI+）
g++ -c -O2 -o build/rgss_image.o src/rgss_image.cpp
# Ruby zlib 扩展
gcc -c -O2 -o build/zlib_ext.o -I$RB -I$RB/win32 \
    -Ithird_party/ruby-1.8.1/ext/zlib third_party/ruby-1.8.1/ext/zlib/zlib.c
# 内嵌 RPG 源码
cp docs/assets/embedded_rpg_classes.rb build/rpg.rb
objcopy -I binary -O elf32-i386 -B i386 \
  --rename-section .data=.rdata,alloc,load,readonly,data,contents build/rpg.rb build/rpg_src.o

# 链接（g++ 带 C++ 运行时，固定基址，关 ASLR）
g++ -shared -O2 -static -Wl,--image-base,0x10000000 -Wl,--disable-dynamicbase \
  -o build/RGSS202E_rebuild.dll build/*.o src/RGSS202E.def \
  -L$RB -lmsvcrt-ruby18-static -lws2_32 -lkernel32 -luser32 -lgdi32 \
  -lwinmm -lmsacm32 -ladvapi32 -lole32 -lz -lgdiplus
```

## 七、未决问题

1. `IsWindowVisible` 定名 85% 置信 → M6 长会话探针可最终确认。
2. JPEG 解码器具体库（版本串 "2.0.8"）、Ogg 版本串 "0.6.0" 对号。
3. Enterbrain 对 Ruby 1.8.1 的本地补丁范围（M3 对拍 5 条全一致，M6 需全量电池）。
4. `transition` 淡入淡出未实现（当前直接切帧）。
5. **自动图块 48 形状 → 四分之一片映射表未还原**（当前用邻接近似，见下）。
6. 音频（MCI/DirectSound）未实现。
7. 鼠标控制（原版 0x13940 前半段的鼠标→方向映射）未实现。

## 八、M5-h 已完成 / 剩余

### ✅ 已完成（本次）
1. **输入键码映射** —— 完整照原版二进制重建：
   - 常量（原版 `0x10104294..0x101042d0` 字符串实测）：
     `DOWN=2 LEFT=4 RIGHT=6 UP=8 A=11 B=12 C=13 X=14 Y=15 Z=16 L=17 R=18
      SHIFT=21 CTRL=22 ALT=23 F5..F9=25..29`
   - `CInput` 布局：`+0x31` 采样 / `+0x4f` 当前 / `+0x6d` 上一帧 /
     `+0x8c` 连发键 / `+0x90` 连发计数 / `+0x94` dir4 / `+0x98` dir8
   - `press?`=当前帧；`trigger?`=本帧按下且上帧未按；
     `repeat?`=连发键且（计数==1 或 ≥24 且 %6==0）
   - VK 映射（原版扫描函数实测）：方向=光标键+小键盘 2/4/6/8（0x62/64/66/68）；
     C=Enter/Space/Z；B=Esc/Numpad0/Insert/X；A=Shift；X=A；Y=S；Z=D；
     L=PageUp/Q；R=PageDown/W；SHIFT/CTRL/ALT=同名键；F5..F9
   - dir4 优先竖直分量（原版 0x13d65 段）
2. **场景切换验证**：标题 → Enter → 新游戏 → Scene_Map → 地图渲染 ✅
3. **Tilemap 数据通路**：`bitmaps`(9 元素)/`passages`/`flash_data`，
   地图 Table 100x100x3 正确装载并绘制 ✅（绘制为近似，见剩余）
4. 一批缺失属性/方法补齐（见缺陷表 19-23）
5. **GC 标记函数**（缺陷 24）与 **Table 头格式**（缺陷 25）两个结构性 bug

### ✅ Window 9 宫格皮肤（本次完成，像素级实测）

皮肤 `Graphics/System/Window.png` (128x128) 布局（逐像素测量 + 与原版截图对照验证）：

| 区域 | 内容 | 绘制方式 |
|------|------|---------|
| (0,0,64,64) | 背景：垂直渐变 (64,127,192)→(0,1,128) | **整块拉伸**（原版 104px 高内部渐变单调无 64px 周期 → 非平铺） |
| (0,64,64,64) | 条纹叠加：(0,0,64,alpha 40) 与透明交替 | 平铺（原版内部逐行 ±15 蓝通道摆动，2px 周期） |
| (64,0,64,64) | 边框：6px 边圈 黑/白/白/192灰/128灰/黑，圆角，内部透明，中心 (17,17)-(47,47) 有箭头 | 9 宫格，切角 6（恰好避开箭头） |
| (64,64,32,32) | 光标：1px 白 + 1px 蓝 + 内部 a32 | 9 宫格，切角 2 |

关键实测锚点（原版截图 x=186..192 与皮肤逐通道吻合）：
`黑,白,白,192,128,黑` 然后内部 → 证明边框区是 (64,0) 且边圈 6px。

坐标约定：contents 画在 (+16,+16)（`create_contents` 用 width-32/height-32），
并随 ox/oy 滚动；`cursor_rect` 是 **contents 相对坐标**（`item_rect` 用 contents.width），
故光标也要 +16,+16。

对比图：`test/Project3_rebuild/window_compare.png`（左原版右重建）

### ⬜ 剩余（下一步）
1. **自动图块 48 形状表**：当前用"四邻同种→内部片，否则角片"的近似，
   海面呈菱形纹、沙路偏金黄。需反汇编原版 Tilemap 绘制函数取得
   48 形状 → 2x3 块内 6 片的精确映射（`build/full_disasm.txt` 可查）。
   已实测锚点：id 2048→kind42→A1#10=深海；id 1537→kind32→A1#0=海滩；
   kind 0-31→TileA2，32-47→TileA1，48-79→TileA3，80+→TileA4。
2. **音频**：MCI（winmm）BGM/ME + SE
3. `transition` 淡入淡出
4. ~~帧率限制~~ (已完成: QueryPerformanceCounter 按 frame_rate 限帧)
5. ~~draw_text 改 GetGlyphOutlineW 自绘~~ (M5-j 已完成)

## 九、字体系统逆向（M5-i，地址标注版）

详见 `docs/M5-i_字体系统逆向.md`。要点:

- **IAT 必须进程内实测**: 之前按字母序猜 IAT 全错。
  真实映射: `GetGlyphOutlineW=0x100FF024`, `EnumFontFamiliesExW=0x100FF028`,
  `CreateFontIndirectW=0x100FF030`, `GetTextMetricsW=0x100FF034`,
  `AddFontResourceExW=0x100FF044`, `GetDeviceCaps=0x100FF048`
- **关键函数**: `0x129A0` 两段式字体创建 (lfWidth 修正才是实际字号来源),
  `0x8F060` 字体名数组可用性过滤, `0x8EF40` 可用性检查,
  `0x12760` CFont 构造 (scale=1.0 / lfHeight 默认 20),
  `0x15D64/0x1602C/0x160A8` GetGlyphOutlineW 字形光栅化
- **size 不换算**: lfHeight 直接 = Font#size; 实际字宽由
  `lfWidth = round(size × scale × tmAveCharWidth / tmHeight)` 决定
  (22 × 21/29 ≈ 16px, 与原版 64x16 吻合)
- **已知差异**: 字形光栅化用 DrawTextW, 原版用 GetGlyphOutlineW 自绘
  （**已在 M5-j 解决** — 改为 GetGlyphOutlineW 自绘，见下）

## 九A、draw_text 自绘字形光栅化（M5-j，地址标注版）

详见 `docs/M5-j_draw_text逆向.md`。要点:

- **原版无 TextOut/DrawText 导入** — 每字符经 `GetGlyphOutlineW(GGO_GRAY8_BITMAP)`
  取 8bit 灰度字形，灰度转 alpha (64→255, 其余×4) 后两遍合成
- **REPLACE 语义**: 合成是直接写 ARGB（替换像素），不是混合。
  Window_Command 的 item 1 被画两次（refresh alpha=255 + draw_item(1,false) alpha=128），
  REPLACE 使得第二次的半透明版本完全替换第一次的不透明版本
- **两段式字体创建** (`0x129A0`): lfHeight=Font#size（无换算），lfWidth 由
  `round(size × tmAveCharWidth / tmHeight)` 修正 — 这才是实际字号来源
- **阴影** = 同一 alpha 图偏移 (+1,+1)，黑色，alpha 用字体颜色的 alpha
- **per-pixel alpha**: 位图的 alpha 字段由窗口合成的 rgss_render_blit 消费
  （`sa = (sp >> 24) & 0xFF`），禁用项的半透明效果由 per-pixel alpha 实现
- **像素验证**: 文字主体 (152,166,210) vs 原版 (146,160,198) ✅；
  阴影边缘 (67,89,157) vs (67,89,157) ✅ **完全一致**
- **已知差异**: 阴影内部略偏亮 (~25/255)，原版混合器 0x96680 可能有 alpha 预乘差异
- 旧 DrawTextW 实现 `#if 0` 死代码保留（按主人决策 1）

## 十、调试工具（本次新增）

| 工具 | 用途 |
|------|------|
| `tools/keytest.c` | 自动化按键测试：启动→截图→keybd_event 注入→再截图 |
| `tools/iat_probe.c` | **进程内读原版 DLL 的 IAT 并反查真实函数名**（IAT 猜错的教训） |
| `tools/blinkprobe.c` | 定时截屏测光标闪烁波形（三角波/40帧/128..255） |
| `tools/cursortest.c` | 验证光标移动与闪烁 |
| `tools/probe_input_consts.c` | 探 Input 常量（需 Phase B，见下） |
| `tools/probe_input2.c` | Input 模块状态深挖 |
| `test/Project3_rebuild/inprobe2.exe` | 完整流程探针：开窗口→GameMain 线程→eval |
| `test/Project3_rebuild/mapprobe.exe` | 读运行中地图数据 |
| `test/Project3_orig/tprobe.exe` | **用原版 DLL 实测 Table._dump 格式** |
| `test/Project3_orig/fonttest*.exe` | 字体渲染实验（GDI 名称解析/尺寸/字形对比） |

注意：`Input` 常量在 Phase B 才注册，单独 `RGSSInitialize2` 后 `Input` 不存在；
探针必须先建窗口 + 调 `RGSSGameMain`（开线程）再 eval。

## 十一、风险与注意事项

- REA 的 IDA headless worker 偶尔掉线 → close_binary + open_binary 重连即可。
  **更好的办法：完全用离线 objdump**（`build/full_disasm.txt`），不依赖 IDA 在线状态。
- 批量反编译偶发单点失败（如 RGSSGameMain@0x100028C8）→ 用 procedure_assembly 手工还原。
- **Bash heredoc 里写 C 代码时 `\n` 会被转义吃掉** —— 改用 Write/Edit 工具写文件，
  或用 `fputc(10, f)` 代替 `\n`。
- 原始 DLL 所在目录是只读原产区，不写入新文件。
- 测试一律用 `test/` 下副本，不动任何原件目录。
- `docs/rgss1/`、`docs/rgss3/`、`tools/RGSS1_*` 属于并行会话的其它项目，不要混淆。
