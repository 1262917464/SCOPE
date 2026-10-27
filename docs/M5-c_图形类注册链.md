# M5-c — 图形类注册链闭环（延迟初始化真相）

日期：2026-10-26 ｜ 方法：运行时反射（`tools/rgss_introspect*.c`）+ 离线 objdump 交叉验证

## 1. 重大结构修正：RGSS 初始化分两阶段

### 阶段 A — `RGSSInitialize2(hwnd)` → `sub_10009E60`（Ruby 层）

```
sub_10034910()                     # ruby_init
eval("$KCODE='U'")
eval("Dir.glob('.')")
rb_define_global_function("load_data"/"save_data")
Init_Audio      (0x10003B60)       # 模块
Init_Table      (0x1000EFA0)
Init_Color      (0x10006950)
Init_Tone       (0x10010210)
Init_Rect       (0x1000C910)
jmp 0x10003440 → eval(内嵌 RPG 源码 blob @0x100FF650)
```

**只有 5 个数据类 + Audio 模块在此注册。**

### 阶段 B — `CGameFrame::Run` 首步 `sub_10009F40`（图形层）

```
sub_10009F40:
  rb_define_class("RGSSError", ...)      # 先建错误类
  rb_define_method(RGSSError, ...)
  eval(...)                              # 0x1000A023 附近的 eval
  sub_10008A80(...)                      # eval 辅助
  Init_Graphics   (0x100084C0)   ← 0x1000A04C
  Init_Bitmap     (0x10006060)   ← 0x1000A051
  Init_Viewport   (0x10011090)   ← 0x1000A056
  Init_Sprite     (0x1000E260)   ← 0x1000A05B
  Init_Tilemap    (0x1000FB50)   ← 0x1000A060
  Init_Plane      (0x1000C150)   ← 0x1000A065
  Init_Window     (0x100122E0)   ← 0x1000A06A
  Init_Font       (0x100075D0)   ← 0x1000A06F
  Init_Input      (0x100087B0)   ← 0x1000A077 (tail jmp)
```

**验证依据**：干净进程只调 `RGSSInitialize2(0)` 后，`Bitmap`/`Graphics`/`Input` 等
全部 `NameError: uninitialized constant`；`Color`/`Tone`/`Rect`/`Table`/`Audio` 则正常。
`0x1000A04C..0x1000A077` 的连续 call 序列与 9 个 Init 地址一一对应（objdump 实测）。

### 为何这样设计（推断）

图形类依赖 GDI 设备上下文与窗口句柄，必须在 `CGameFrame::Run` 拿到 hwnd 并完成
窗口子类化之后才能注册；而 `RGSSInitialize2` 只做与窗口无关的 Ruby 环境准备。
这解释了 M4 报告中 `Run` 第一步 `sub_10009F40()` 的存在。

## 2. 运行时反射确认的事实（原版 DLL，`RGSSInitialize2(0)` 后）

| 项 | 实测结果 |
|----|---------|
| Color 方法 | `== === _dump alpha alpha= blue blue= eql? green green= red red= set to_s` + 单例 `_load` |
| Tone 方法 | 同上但 `gray/gray=` 替代 alpha |
| Rect 方法 | `== === _dump empty eql? height height= set to_s width width= x x= y y=` + `_load` |
| Table 方法 | `[] []= _dump resize xsize ysize zsize` + `_load` |
| Audio 方法 | `bgm_fade bgm_play bgm_stop bgs_fade bgs_play bgs_stop me_fade me_play me_stop se_play se_stop` |
| Color#set arity | -1（可变参数） |
| Rect#set arity | 4 |
| Table#[] arity | -1 |
| Color.new(1,2,3) | `(1.000000, 2.000000, 3.000000, 255.000000)` — 浮点存储，alpha 默认 255 |
| Rect.new(1,2,3,4) | `(1, 2, 3, 4)` — 整数 |
| Object.constants | 含 `RPG Audio Color Rect Table Tone Zlib Win32API` 等，**不含** Bitmap/Sprite/…（阶段 B 才注册） |

## 3. 对最终 DLL 的意义

重建实现必须**同样分两阶段注册**：
- `RGSSInitialize2` 里只注册 Audio/Table/Color/Tone/Rect + eval RPG 源码
- `CGameFrame::Run` 开头注册 9 个图形类

否则游戏脚本在 `Graphics.update` 前访问 `Bitmap` 会得到与原文不一致的 NameError 行为。

## 4. 待续（M5-c 后续）

- 9 个图形类 Init 函数体的逐方法解码（同 328 条表的方法，objdump 机械提取）
- 各图形方法的 C 实现还原（Bitmap 的 blt/draw_text 优先）
- Nx/CSx 后端与最终 DLL 集成
