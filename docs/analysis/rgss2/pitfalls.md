# RGSS2 重建踩坑记录 — 缺陷 45 条 + Ruby 1.8.1 C API 陷阱

> 提取自重构前的根 `PROGRESS.md` §五（2026-10-27），属**长期参考事实**：
> 绝大多数条目记录的是"原版 Ruby 1.8.1 / RGSS 语义的客观陷阱"，即使日后重写实现也依然成立。
> 与 `docs/audit/` 的区别：审计是某次快照的判定（会过期），这里是可复用的经验事实。
> 编号与原 PROGRESS.md 保持一致，外部引用（如代码注释"按主人决策 1"）以本文件为锚。

## 缺陷表（45 条）

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
| 46 | **★文字混合用 a/255 权重的 REPLACE/over, 而非原版 F[a]=a+1 查表式★** | **阴影明显偏淡** (暗环带 B 通道 121.8 vs 原版 93.0, 最暗点 53.7 vs 32.6) | 照原版混合器精确还原 (见 M5-k §八): `out_c = dst_c + (src_c−dst_c)×F[a]>>8` 四通道, F[0]=0/F[a]=a+1 (0x100A9790 初始化), 字体 alpha 走 src 的 alpha 通道。修正后暗环带 96.1 vs 93.0, 最暗点 35.1 vs 32.6 (残差来自窗口背景 +9B 偏差), 近白像素比 0.997 |
| 47 | **★contents→屏幕合成用直通 alpha, 源色未按字体 alpha 预乘★** | **文字边缘偏瘦** (f² 双重乘, 字芯 131 vs 175); 禁用项饱和 255 vs 155 | 原版管线: 混合器对透明 dst 产出预乘值 (rgb=src×f, a=f), 源色 tile 由同一混合器生成 (rgb 已按 fa 预乘), contents→屏幕为预乘 over (screen=rgb+dst×(1−a))。实现: write_over 源色预乘 + render_blit 内部 mode 3 (预乘 over, 仅 contents 合成)。**mode 3 必须钳制** (预乘 rgb + bg 残余 >255, 溢出致禁用项变黄) |

## Ruby 1.8.1 C API 陷阱（写代码必看）

1. **vararg 签名**：`rb_define_method(..., -1)` 必须是 `VALUE f(int argc, VALUE *argv, VALUE self)`。
   写成 `(VALUE self, int argc, ...)` 直接段错误 —— 这是黑屏根因。
2. `Data_Wrap_Struct(klass, mark, free, data)` 四参；free 为 `void (*)(void*)`。
3. `uint32_t` 需显式 `#include <stdint.h>`（Ruby 1.8 头不含）。
4. `RB_TYPE_P`/`RSTRING_PTR` 是 1.9+ 命名 → 用 `TYPE(v) == T_STRING` / `RSTRING(v)->ptr`。
5. `rb_mMarshal` 不存在 → `rb_const_get(rb_cObject, rb_intern("Marshal"))`。
6. `Marshal.load` 接受**字符串数据或 IO**，不是文件名。
7. `RGSSGetStringUTF8` 用静态缓冲，连续调用互相覆盖（原版同样行为）。

## 逆向定性判据（遇"用途不明函数"必看）

> 来源：0x1008EF40 两次误判事故（M5-i 误判为"字体可用性过滤"→ M5-k 误判为"防盗版联网校验"
> → 2026-10-28 主人质疑后对质 Ruby 源码，实为 `win32.c is_socket()`）。三条判据，按序使用：

1. **Ruby 源码优先**：原版静态链接了完整 Ruby 1.8.1，DLL 里大量"怪函数"就是 Ruby
   自身（win32.c/socket.c/regex.c/…）。任何含 winsock 错误码、TLS、临界区、stdio
   模式、fd_set（count + 64 元素数组 = FD_SETSIZE）特征的函数，先拿
   `local/third_party/ruby-1.8.1/` 官方源码 grep 对质（搜常量比搜函数名快：
   如 `WSAENOTSOCK`/`0x1008`+`0xffff` 参数对），定性后才允许写进文档。
2. **常见常量指纹速查**：`0xffff`+`0x1008` = getsockopt(SOL_SOCKET, SO_TYPE)；
   `0x2736/0x276d` = WSAENOTSOCK/WSANOTINITIALISED；`0x94` = OSVERSIONINFOW 尺寸；
   `0x40` 上限循环 = FD_SETSIZE 或 64 字节族名槽。见到即查 Ruby，别发明语义。
3. **二手结论入档前必须复核**：审计线引用开发线文档里的定性（或反之）时，
   要么自己验证到证据级，要么在清单里标注"转引自 X，未验证"。
   反例：差异清单 B48 曾基于 M5-k §4.2 登记"防盗版校验"，实际不存在（见
   `docs/analysis/rgss2/差异清单.md` §三-A）。
