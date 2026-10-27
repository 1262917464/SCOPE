# M5-m Graphics 转场与 fade 逆向（freeze / transition / fadein / fadeout）

> 白盒证据全部来自离线 objdump（`local/build/full_disasm.txt`，基址 0x10000000），
> 无一处目测反推。日期 2026-10-28。

## 一、方法注册表（权威清单）

Graphics 模块注册块 0x100084c0 起（rb_define_module_function @0x10021250，
基址 0x10172014 单例）。完整 18 项：

| 方法 | C 函数 | argc |
|------|--------|------|
| _reset | 0x10007ab0 | 0 |
| update | 0x10007b10 | 0 |
| wait | 0x10007b50 | 1 |
| fadeout | 0x10007e30 | 1 |
| fadein | 0x10007ef0 | 1 |
| freeze | 0x10007bb0 | 0 |
| transition | 0x10007fb0 | -1 |
| snap_to_bitmap | 0x100083a0 | 0 |
| frame_reset | 0x10007c40 | 0 |
| resize_screen | 0x10007c60 | 2 |
| frame_rate | 0x10007cc0 | 0 |
| frame_rate= | 0x10007ce0 | 1 |
| frame_count | 0x10007d30 | 0 |
| frame_count= | 0x10007d50 | 1 |
| brightness | 0x10008460 | 0 |
| brightness= | 0x10007d90 | 1 |
| visible | 0x100084a0 | 0 |
| visible= | 0x10007de0 | 1 |

单例关键字段：+0x214 frame_rate、+0x218 frame_count、+0x244 屏幕/实况层、
+0x250 全屏黑幕精灵、+0x254 转场源快照、+0x258 渲染器、+0x25c 转场位图。

## 二、帧管线归属

- `0x100014a0` = F12 检查（GetKeyState 0x7B，按下 raise "Reset"）。
- `0x10001640` = 限帧器（timeGetTime，1000/frame_rate，最多连跳 4 帧，
  100ms 窗口复位 FPS 统计）。
- `0x10001890` = 帧步进：**frame_count++ 在 0x10001946**，随后合成、显示、
  鼠标客户区换算（GetCursorPos/ScreenToClient 0x100ff35c/358）、关窗检查。
- update/wait/fadeout/fadein/transition 的每一帧 = 0x10001640 + 0x100014a0
  + 0x10001890 同一组合 → 重建版抽成 `gfx_step_frame()`。

## 三、fadein / fadeout（逐指令公式）

黑幕强度 L 存于 +0x250 精灵属性 0x10（0x10014770 写入）；brightness= setter
0x10007d90 钳制 0..255 后写 255-v。故 **L = 255 − brightness**。

- fadeout(d) 0x10007e30：C=当前 L（从 +0x250 遮罩块 +0x8c 的 dword 回读，
  fadeout 0x10007e6c `rep movs` 14 dwords 后取 [esp+0x44]）；
  d<1 跳环（0x10007e6e `cmp ebx,1 / jl`）；每帧
  `L = C + (i+1)*(255-C)/d`（0x10007e80 div + add），帧步进；
  尾声 L=255（0x10007ecf）。
- fadein(d) 0x10007ef0：每帧 `L = C − (i+1)*C/d`（0x10007f40）；
  尾声 L=0（0x10007f8f）。

## 四、freeze（0x10007bb0）

0x10095ba0/0x10095bf0 抓当前帧到 +0x254 → 0x10098a10(+0x244,0) 隐藏实况层 →
0x1009a280(+0x258,1,&{0}) 写转场参数块 → 渲染器 vtable+4(1) 进转场显示。
效果：此后画面静止显示快照，游戏逻辑继续跑，直到 transition 结束转场。

## 五、transition（0x10007fb0）

- argc>3 → ArgumentError（0x10008f20）。
- duration 默认 10（0x1000804d `mov ebx,0xa`）；filename 默认 ""（0x10008052
  `ebp=0x100ff55e`）；vague 默认 0x28=40（0x10008057）。
- 进转场 vtable+4(1)（0x100080b8）→ 黑幕清零（0x100080d6 push 0,0x10）。
- 文件名判定只比较**首字节**（0x100080e9 `mov ecx,1; repz cmpsb`）。
- 默认环 0x100082a0：每帧冻结图权重 `a = 255 − (i+1)*255/dur`（0x100082b4
  div + `sub ecx,eax`，esi 每轮 +0xff），帧步进；dur<1 跳环。
  即冻结图权重线性 255→0 的 crossfade。
- 文件环 0x10008238：参数块 {0x1000, 0x100, vague, 步进值} 交给渲染器，
  遮罩混合发生在 D3D 着色器 —— **未解码（已知差异）**，重建版退化为同参数
  crossfade（见 §七）。
- 尾声 0x1003f2e0 → vtable+4(0)（0x10008330）→ 0x10013c40 释放 +0x25c
  （0x1000833e；0x100086c0 亦释放）。

## 六、其它按反汇编顺手修正

- frame_rate= 0x10007ce0：≤10 → 10，>120 → 120。
- snap_to_bitmap 0x100083a0：取 [0x244] 矩形 (+0x8..+0x14) 尺寸新建 Bitmap
  并拷当前帧（此前重建版返回空位图）。
- _reset 0x10007ab0：frame_rate=60、黑幕清零、退出转场、释放快照。
- **缺陷 48**：argc=-1 注册的 C 函数必须用 `(int argc, VALUE *argv, VALUE self)`
  签名，首版写成 `(m, argc, argv)` 导致 self 被当 argc。
- **缺陷 49**：Window/Plane/Tilemap dispose 原为空操作 → Scene_Menu#terminate
  后 @status_window 残留画面。dispose 置 disposed=1 + visible=0 + 脱离注册表，
  compose 各层跳过 disposed。

## 七、已知差异（记录在案）

1. **文件转场遮罩着色器未解码**：0x10008238 文件环只把 {4096, 256, vague,
   步进} 写进渲染器参数块，逐像素混合在 D3D shader 内。重建版对 filename≠""
   退化为同参数 crossfade。战斗转场（BattleStart 等）观感为淡入而非遮罩扫描。
   后续：解码渲染器 +0x8c 参数块消费侧与 shader 字节码。
2. transition 尾声的 0x1003f2e0 与限帧基线复位 0x10001610 未逐一复刻
   （重建版限帧器落后时自动重对齐，行为等价）。

## 八、验证（difftest 6 帧，Project3_rebuild vs Project3_orig）

| 帧 | 字节差 | 说明 |
|----|--------|------|
| f00_title | 1.20% | 与 M5-k 基线持平 |
| f01_map | 73.44% | **与 M5-k 旧重建版逐字节相同（0.00%）**——既有自动图块/菱形点阵缺陷，非本次回归 |
| f02_down / f03_left | 73.2% | 同上 |
| f04_menu | 54.36% | 菜单本体一致，差异为背景地图缺陷 |
| f05_closed | 73.52% | 菜单关闭后无窗口残留（dispose 修复生效） |

另：Graphics.transition 实装后 Scene_Title/Scene_Map/Scene_Menu 的进出
转场按原版语义运行（冻结→crossfade），不再是一次裸 update。

## 九、涉及文件

- `src/runtimes/rgss2/graphics_classes.c`：gfx_step_frame / gfx_capture_freeze /
  gfx_apply_display_rule / freeze / transition / fadein / fadeout /
  brightness(±) / frame_rate=(钣制) / snap_to_bitmap / _reset / dispose 族
- `src/runtimes/rgss2/bitmap.c`：rgss_bitmap_copy_bits（snap_to_bitmap 用）
- `src/runtimes/rgss2/gamemain.c`：异常落盘 rgss_error.log（走 RGSS_LOG_FOPEN）
- `src/runtimes/rgss2/debug.h`：日志清单登记 rgss_error.log
