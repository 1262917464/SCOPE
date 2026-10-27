# A3 — Graphics / Sprite / Viewport 与原版二进制逻辑一致性审计
> 注：本文写于 2026-10-27 仓库重构前；文中 `backup/` `unpacked/` `build/` `test/` `third_party/` 等路径现位于 `local/` 下，`src/rgss_*.c` 现为 `src/runtimes/rgss2/*.c`，`tools/` 已按流水线分桶（unpack/pe/analysis/probe/verify）。


- 审计日期: 2026-10-27
- 对比材料: `build/full_disasm.txt`(原版 objdump, 基址 0x10000000, 脱壳版),
  `docs/analysis/rgss2/rgss_ruby_api_map.tsv`(97 条), 字符串证据取自
  `unpacked/RGSS202E_unpacked.dll`(.rdata 直接读取)。
- 重建源码: `src/rgss_graphics_classes.c`, `src/rgss_render.c`, `src/rgss_gamemain.c`(只读审计, 未改动)。
- 方法: 逐条按 TSV 地址读原版反汇编, 与重建实现对照。原版通用约定(适用于全部条目):
  - `0x10026860(self,0x22)` = 对象守卫; 之后 `native==0 → rb_raise(0x10172024=RGSSError, "disposed sprite"/"disposed viewport")`(字符串 0x101048a8 / 0x10104a70)。
  - `0x1001d850` = 整数→VALUE(INT2FIX, 溢出走 Bignum); `0x100552c0` = rb_float_new。
  - `0x1008bf70/0x1008bf90` = 按名字符串的 ivar get/set 辅助; ivar 名实测:
    0x101047d0="viewport" 0x101047dc="bitmap" 0x101048b8="src_rect"
    0x10103f90="rect" 0x10103e10="color" 0x101047c0="tone" 0x10103e58="owner" 0x10103e74="observer"。
  - Sprite/Viewport 构造时给 src_rect/rect/color/tone 对象写 `owner`(=自身)与
    `observer`(=:src_rect= / :rect= / :color= / :tone= 符号), 实现 Rect.set→精灵 回写联动。

## 判定计数

| 判定 | Graphics(20) | Sprite(56) | Viewport(21) | 合计(97) |
|---|---|---|---|---|
| 一致 | 4 | 15 | 10 | 29 |
| 等价 | 4 | 17 | 7 | 28 |
| 偏差(详述) | 7 | 13 | 4 | 24 |
| 桩 | 3 | 3 | 2 | 8 |
| 未映射(重建缺失) | 2 | 2 | 0 | 4 |
| 无法验证 | 0 | 6 | 0 | 6 |

- 申报过的已知偏差: Graphics.transition(确认, 详见下)。
- 未申报的新偏差共 23 项(transition 除外), 其中影响行为最大的:
  **Graphics.frame_rate= 无 [10,120] 钳制、frame_reset 语义相反、brightness= 无视觉效果、
  Sprite/Viewport clone/dup 未覆盖、Graphics width/height/visible/visible= 与 Sprite width/height
  共 6 个方法缺失、dispose 后访问器不抛 RGSSError、sprite 默认 color 的 alpha 为 255(原版 0)、
  viewport 合成(裁剪/层次/z)整体缺失**。

---

## 一、Graphics(20 条, 0x10007ab0–0x100084a0)

原版全局对象: `0x1016efe4` = CGameFrame*; 字段 +0x214=frame_rate, +0x218=frame_count,
+0x220..0x22c=FPS 计时基线/累计/计数/重试, +0x230=实测 FPS, +0x244=屏幕对象,
+0x248=渲染列表, +0x250=亮度/混合参数结构, +0x254=转场对象, +0x258=窗口对象。
内部辅助: `0x100015f0`=frame_rate=, `0x10001600`=frame_count=, `0x10001610`=FPS 计时基线重置,
`0x10001640`=帧节流+呈现(节流通过时调 0x10097350 画一帧), `0x100014a0`=F12→raise Reset,
`0x10001890`=消息泵+frame_count++(0x10001946)+节流等待循环, `0x10001810`=单条消息泵。
`0x100ff598`="exit 0"(窗口关闭时经 0x10033050 eval 触发 SystemExit)。

| class.method | 原版地址 | 判定 | 证据摘要 |
|---|---|---|---|
| Graphics._reset | 0x10007ab0 | 偏差(详述) | 原版: frame_rate←60(0x100015f0,常量0x3c)、brightness 参数←0(0x10014770(0x10,0) 即恢复全亮)、+0x258 虚表+4(0)、0x10096ad0(+0x254,0,0) 复位转场对象; **不**清 frame_count。重建 gfx_reset(src:177) 只做 `g_frame_count=0; g_frozen=0` — 语义不同且丢失帧率/亮度复位。 |
| Graphics.update | 0x10007b10 | 等价 | 原版序列: 0x10001640(节流+到点才呈现)→0x100014a0(F12→Reset 异常)→0x10001890(泵消息、frame_count++、忙等)。重建 gfx_update(src:56-112): 泵消息、QPC 节流(等价于 1000/frame_rate 预算+重试)、frame_count++、clear+compose+present。核心语义(限帧/计数/绘制)一致。差异备注: 重建无窗口关闭时 eval("exit 0") 退出路径, 无 timeBeginPeriod(3)(原版 0x1000189b), 重建每次调用都重新合成而原版到点才呈现。 |
| Graphics.wait | 0x10007b50 | 等价 | 原版: n≤0 跳过(0x10007b67 jle), 否则每帧 0x10001640+0x100014a0+0x10001890+exit0 检查。重建 gfx_wait(src:140) 每帧调 gfx_update, n≤0 不循环。帧数语义一致。 |
| Graphics.fadeout | 0x10007e30 | 桩 | 原版: 保存 +0x250+0x8c 的 14 dword 混合参数(0x10007e63 rep movs), 逐帧 0x10014770(0x10, 亮度步进 esi/frames) 渐暗 + 每帧完整 update 三件套(0x10007e94-0x10007ec6), 结束 0x10014770(0x10,0xff)=全黑; frames<1 跳过。重建 gfx_fadeout(src:146) `return Qnil` 无任何效果。 |
| Graphics.fadein | 0x10007ef0 | 桩 | 同 fadeout 镜像(0x10007f40 循环, 结束 push 0 → 亮度恢复 255)。重建 gfx_fadein(src:147) 无效果。 |
| Graphics.freeze | 0x10007bb0 | 桩 | 原版: 0x10095ba0 抓当前帧 → 0x10097610 存入 +0x244 → 0x10095bf0 → 0x10098a10(0) → 0x1009a280(转场准备) → +0x258 虚表+4(1)。重建 gfx_freeze(src:127-133) 仅 `g_frozen=1`, 带 TODO 未拷快照。 |
| Graphics.transition | 0x10007fb0 | 偏差(详述) | 已申报。原版 ≈1000 行: 0-3 参数(duration[,filename[,vague]]), filename 与空串(0x100ff55e) repz cmps 比较, 调 0x10001610 重置计时, 两个逐帧循环(0x10008265/0x100082e2)各调 0x10001640+0x100014a0+0x10001890+exit0 检查, 用冻结快照 0x10095cb0 做淡出/淡入交叉混合。重建 gfx_transition(src:134-139) 只调一次 gfx_update — 无转场、无等待 duration 帧。 |
| Graphics.snap_to_bitmap | 0x100083a0 | 偏差(详述) | 原版: 以屏幕区([+0x244]+0x10-+0x8, +0x14-+0xc) 新建 Bitmap(0x100062b0), 再把屏幕内容 blit 进去(0x10095ba0/0x10097610), 返回带画面的位图。重建 gfx_snap_to_bitmap(src:148-154) 仅 `Bitmap.new(g_screen_w, g_screen_h)` — 尺寸对, 内容全空。 |
| Graphics.frame_reset | 0x10007c40 | 偏差(详述) | 原版调 0x10001610: timeGetTime 存 +0x220, 清 +0x224/+0x228/+0x22c — **只重置 FPS 测量基线, 不动 frame_count(+0x218)**。重建 gfx_frame_reset(src:123) 清零 `g_frame_count` — 语义相反(frame_count 在脚本层常被用作计时)。 |
| Graphics.resize_screen | 0x10007c60 | 偏差(详述) | 原版经 0x10001bc0: 钳制 w∈[1,640](cmp 0x280)、h∈[1,480](cmp 0x1e0), 尺寸变化时 0x10097830 实际重设屏幕。重建 gfx_resize_screen(src:155-161) 只改 g_screen_w/h(且无钳制), 后缓冲仍为创建时的 544x416(rgss_render_init 只调一次, src:32)。 |
| Graphics.width | 0x10008420 | 未映射 | 原版 = [+0x244]+0x10 − +0x8(屏宽)→INT2FIX。重建 rgss_init_graphics(src:874-894) 未注册 `width`, 脚本调用即 NoMethodError。 |
| Graphics.height | 0x10008440 | 未映射 | 同上(+0x14 − +0xc)。重建未注册。 |
| Graphics.frame_rate | 0x10007cc0 | 一致 | 读 +0x214 → INT2FIX。重建返回 g_frame_rate(src:114)。 |
| Graphics.frame_rate= | 0x10007ce0 | 偏差(详述) | 原版钳制 **[10,120]**: ≤10 → 存 10(0x10007cfe mov eax,0xa), >120 → 存 120(0x10007d13 cmp 0x78), 存 +0x214, 返回原参数。重建 gfx_frame_rate_set(src:115) 无任何钳制。 |
| Graphics.frame_count | 0x10007d30 | 一致 | 读 +0x218 → INT2FIX。重建一致。 |
| Graphics.frame_count= | 0x10007d50 | 一致 | 0x10001600 直存 +0x218, 返回原参数。重建一致(src:117-122)。 |
| Graphics.brightness | 0x10008460 | 一致 | 原版拷 +0x250+0x8c 结构, 返回 255 − 参数[13](0x10008481 sub ecx,[esp+0x3c])。重建 g_brightness 初值 255, 数值语义等价。 |
| Graphics.brightness= | 0x10007d90 | 偏差(详述) | 原版: 钳制 [0,255](0x10007dab jge / 0x10007db1 cmp 0xff), **立即应用** 0x10014770(0x10, 255−v) 到屏幕亮度叠加层。重建 gfx_brightness_set(src:125) 只存 g_brightness, 渲染管线从不使用 → 无视觉效果(fadeout/fadein/transition 依赖它, 连带失效)。 |
| Graphics.visible | 0x100084a0 | 未映射 | 原版读 [+0x244]+0x20 符号位 → Qtrue/Qfalse(0x100084ae neg/sbb/and 2)。重建未注册。 |
| Graphics.visible= | 0x10007de0 | 未映射 | 原版: Qnil/Qfalse → +0x244 虚表+4(0), 否则 (1); 返回 true/false。重建未注册。 |

## 二、Sprite(56 条, 0x1000cb30–0x1000e1f0)

原版结构: native 0x2b8 字节, 构造 0x10015ad0(实测默认: fld1→zoom=1.0, +0x120=0x80→bush_opacity=128,
+0x104=0xb4/+0x108=0x168→wave 计时默认 180/360, opacity 相关 0xff)。
初始化(0x1000cb30): viewport 参数为 nil 时挂默认屏幕([game+0x244], 0x1000cb7c), 
@src_rect=Rect.new(0,0,0,0)(0x1000cb00)+owner/observer 链接, @color=Color.new(0,0,0,0)(0x10006b20), @tone=Tone.new()(0x100103e0)。

| class.method | 原版地址 | 判定 | 证据摘要 |
|---|---|---|---|
| Sprite.alloc_func | 0x1000e1f0 | 一致 | 分配 12 字节 native 包装(0x1003f5a0(0xc))+插入全局渲染链表(0x10010e60)。重建 sprite_alloc(src:418) calloc+Data_Wrap_Struct, 注册移到 initialize — 均为"创建即进列表", 等价。 |
| Sprite.clone | 0x10008fb0 | 偏差(详述) | 原版无条件 raise(0x101be784, "can't clone %s"@0x1010436c, 0x10008fb5→0x1008c150 后 0x10025f80)。重建未覆盖 clone → 继承 Object#clone 成功浅拷贝。 |
| Sprite.dup | 0x10008fd0 | 偏差(详述) | 同上, "can't dup %s"@0x1010437c。重建未覆盖。 |
| Sprite.initialize | 0x1000cb30 | 等价 | 原版: @src_rect=Rect(0,0,0,0), @color=Color(0,0,0,0), @tone=Tone(0,0,0,0), viewport=nil→默认屏幕。重建 sprite_initialize(src:421-442): src_rect=Rect(0,0,0,0)、color/tone 置 nil 由 getter 懒创建(值相同)、bush_opacity=128、zoom=1.0、opacity=255 与 native 构造常量一致。差异备注: 原版 color/tone 立即创建; 原版有 owner/observer 回写链接, 重建无(合成每帧直读 VALUE, 视觉等价)。 |
| Sprite.dispose | 0x100113f0 | 等价 | 原版幂等: native 非空则虚表[0](1) 释放并置 0, 不抛错。重建置 disposed 标志(src:444), 合成时跳过(src:1499)。 |
| Sprite.disposed? | 0x10011420 | 一致 | native==0 → Qtrue(neg/sbb)。重建一致。 |
| Sprite.flash | 0x1000dde0 | 桩 | 原版: color 可省(0x1000def Qnil 分支), 时长转换后 0x10014690(native,duration) 写入 flash 状态, 由 update 推进。重建 sprite_flash(src:522) 直接 return Qnil。 |
| Sprite.update | 0x1000ccd0 | 桩 | 原版调 0x100157b0 推进 flash/波形计时。重建 sprite_update(src:523) no-op — flash/wave 全部失效(与 flash 桩互为因果)。 |
| Sprite.width | 0x1000cd10 | 未映射 | 原版 = @src_rect 的 native+0x10(即 src_rect.width), disposed 守卫。重建未注册 `width`/`height`(rgss_init_sprite src:946-1001 无此二方法)。 |
| Sprite.height | 0x1000cd70 | 未映射 | 同上(native+0x14)。 |
| Sprite.viewport | 0x1000cdd0 | 一致 | 返回 ivar "viewport"; 未传参时 ivar 未设 → nil, 重建同样返回 nil。 |
| Sprite.viewport= | 0x1000ce10 | 偏差(详述) | 原版: Qnil → @viewport=nil 并 native 虚表+0x1c([game+0x244]) 挂回主屏; 非 nil 时类型检查+若对象变化才 native 切换。重建 sprite_viewport_set(src:500) 仅存 VALUE; 且合成管线(rgss_compose_scene src:1485-1525)完全忽略 viewport — 不裁剪、不参与层次/z。 |
| Sprite.bitmap | 0x1000cec0 | 一致 | 原版此 getter 无 disposed 守卫, 直接返回 ivar "bitmap"。重建一致。 |
| Sprite.bitmap= | 0x1000dce0 | 等价 | 原版: nil → @bitmap=nil+native(0,0); 非 nil → 类型检查(0x10171ffc=Bitmap)、**与当前相同则整体跳过(0x1000dd4a je)**, 否则设 ivar+native(vtbl+0x2c) 并把 @src_rect 清零后填 (0,0,w,h)(0x1000dda4-0x1000ddcf, w/h 取自 bitmap native)。重建 sprite_bitmap_set(src:474-486) 同样重置 src_rect=(0,0,w,h)。差异备注: 重建对相同 bitmap 重复赋值也会重置 src_rect(原版幂等跳过)。 |
| Sprite.src_rect | 0x1000cee0 | 等价 | 返回内部 Rect(永不为 nil, initialize 已建)。重建懒创建路径实际不可达(initialize 必建), 行为等价。 |
| Sprite.src_rect= | 0x1000cf20 | 偏差(详述) | 原版**不替换 ivar**: 读 @src_rect(0x1000cf6b), 把参数 Rect native 的 x,y,x+w,y+h 拷进内部 Rect native(0x1000cfc9-0x1000cfd0) 并同步 native 虚表+0x30 — `sprite.src_rect` 恒为同一 Rect 对象。重建 sprite_src_rect_set(src:498) 直接替换引用 `sp->src_rect = v`, 对象标识与原版不同; 且无 observer 回写。 |
| Sprite.visible | 0x1000da60 | 一致 | native+0x20 符号位 → Qtrue/Qfalse。重建一致。 |
| Sprite.visible= | 0x1000cfe0 | 等价 | 原版 RTEST→native(1/0), **返回归一化的 true/false**(esi=2/0)。重建返回原参数(轻差异, src:469)。 |
| Sprite.x / x= | 0x1000d040 / 0x1000d080 | 偏差(详述) | 原版 getter 返回 **Integer**(native+0xd4 → 0x1001d850 INT2FIX/Bignum); setter 0x100155c0 无钳制。重建返回 rb_float_new(Float)(src:451) — 类型不同(值比较/序列化可见); setter 等价。 |
| Sprite.y / y= | 0x1000d0e0 / 0x1000d120 | 偏差(详述) | 同 x(native+0xd8, setter 0x100155e0)。重建 Float vs 原版 Integer。 |
| Sprite.z / z= | 0x1000daa0 / 0x1000d180 | 偏差(详述) | 同 x(native+0x3c, setter 虚表+0xc)。重建 Float vs Integer; z 用于重建 sort_sprites(src:1193) 排序, 数值语义等价。 |
| Sprite.ox / ox= | 0x1000d1e0 / 0x1000d220 | 偏差(详述) | 同 x(native+0xdc, setter 0x10015600)。类型差异同上。 |
| Sprite.oy / oy= | 0x1000d280 / 0x1000d2c0 | 偏差(详述) | 同 ox(native+0xe0, setter 0x10015620)。类型差异同上。 |
| Sprite.zoom_x / zoom_x= | 0x1000d320 / 0x1000d360 | 无法验证 | 原版 getter 返回 Float(native+0xe8 → 0x100552c0, 已确认), setter 0x10015640 未逐指令核对; 重建直存 double。类型一致, 数值路径未完整验证。 |
| Sprite.zoom_y / zoom_y= | 0x1000d3b0 / 0x1000d3f0 | 无法验证 | 同 zoom_x(native+0xf0)。 |
| Sprite.angle / angle= | 0x1000d440 / 0x1000d480 | 无法验证 | 原版 getter 返回 Float(native+0xf8, 已确认); setter 未核对。**重建存储了 angle 但合成管线完全不使用(无旋转)** — 见渲染管线偏差 R3。 |
| Sprite.wave_amp / = | 0x1000d4d0 / 0x1000d510 | 无法验证 | 原版默认 native 构造里 wave 相关=180/360(0x10015b6a/0x10015b74), 重建默认 0; wave 渲染未实现, getter/setter 逐指令未核对。 |
| Sprite.wave_length / = | 0x1000d570 / 0x1000d5b0 | 无法验证 | 同上。 |
| Sprite.wave_speed / = | 0x1000d610 / 0x1000d650 | 无法验证 | 同上。 |
| Sprite.wave_phase / = | 0x1000d6b0 / 0x1000d6f0 | 无法验证 | 同上。 |
| Sprite.mirror / mirror= | 0x1000d740 / 0x1000d780 | 一致 | bool 归一化返回。重建一致(src:470-471); 备注: mirror 在重建合成中未生效(见 R3)。 |
| Sprite.bush_depth / = | 0x1000d7e0 / 0x1000d820 | 一致 | 原版整型存取。重建 INT2FIX/NUM2INT 一致; 备注: bush 渲染未实现(R3)。 |
| Sprite.bush_opacity / = | 0x1000d880 / 0x1000d8c0 | 一致 | 原版默认 0x80=128(0x10015b8a), 重建 initialize 也设 128(src:439); 渲染未实现(R3)。 |
| Sprite.opacity | 0x1000dae0 | 等价 | 原版从 +0x250 式混合结构读(数值语义=设定值)。重建直存。 |
| Sprite.opacity= | 0x1000db40 | 偏差(详述) | 原版**钳制 [0,255]**(0x1000db87 test sign / 0x1000db8f cmp 0xff)后写入混合结构。重建不钳制(src:459 宏直存) → opacity>255 时 rgss_render_blit(src:123 `sa*opacity/255`) alpha 溢出 >255, 混色结果异常。 |
| Sprite.blend_type | 0x1000dbd0 | 等价 | 原版 getter 返回 value & 7(0x1000dc14 and ecx,7)。重建直存原值; 常规值 0/1/2 下等价。 |
| Sprite.blend_type= | 0x1000dc30 | 等价 | 原版按位写入混合结构((old&~7)|v&7)。重建直存; rgss_render_blit 仅处理 0/1/2(default→普通)。备注: 重建未掩码, 非法值(如 3+)原版回读会被 &7 折叠, 重建原样返回。 |
| Sprite.color | 0x1000d920 | 偏差(详述) | 原版返回 initialize 创建的 **Color.new(0,0,0,0) — alpha=0**。重建 sprite_color 懒创建 (0,0,0,255)(src:506 `INT2FIX(255)`) — 默认 alpha 255 ≠ 原版 0(r/g/b 相同)。 |
| Sprite.color= | 0x1000df40 | 等价 | 原版同步内部 Color native 字节并写 native 打包色(同 Viewport.color= 的 0x100145e0 路径, 逐字节 fistp 截断); 重建存引用, 合成每帧直读 — 常规脚本用法等价。备注: color 在重建合成中未生效(R3)。 |
| Sprite.tone | 0x1000d960 | 一致 | 原版返回 Tone.new()=(0,0,0,0); 重建懒创建 (0,0,0,0)(src:516) 值等价。 |
| Sprite.tone= | 0x1000d9a0 | 等价 | 原版同步 4 double 到内部 Tone native(0x1000da37-0x1000da50)并写 native(0x100145e0/0x10014610); 重建存引用直读。备注: tone 渲染未实现(R3)。 |

## 三、Viewport(21 条, 0x10010420–0x10011020)

原版结构: native 0xc8 字节, 构造 0x100182d0(挂默认屏幕 [game+0x244])。
初始化(0x10010420): `rb_scan_args(1..4)` — 1 参须为 Rect(0x1001044c 用 Rect 类 0x1017b4c8 检查,
取其 x,y,w,h), 否则 4 参; @rect=内部 Rect(owner/observer=:rect=), @color=Color(0,0,0,0), @tone=Tone()。

| class.method | 原版地址 | 判定 | 证据摘要 |
|---|---|---|---|
| Viewport.alloc_func | 0x10011020 | 一致 | 与 Sprite.alloc 同构(0xc 字节包装+0x1017b500/0x1017b4fc 全局链表)。重建 vp_alloc 等价。 |
| Viewport.clone | 0x10008fb0 | 偏差(详述) | 共享 Sprite 的实现, raise "can't clone %s"。重建未覆盖。 |
| Viewport.dup | 0x10008fd0 | 偏差(详述) | 同上, "can't dup %s"。重建未覆盖。 |
| Viewport.initialize | 0x10010420 | 偏差(详述) | 原版强制 1(Rect) 或 4 参数(0x10008f20(argc,1)/(argc,4)), Viewport.new() → ArgumentError。重建 vp_initialize(src:543-556) `rb_scan_args("04")`, 0 参时默认 (0,0,544,416) — 参数协议放宽。其余(rect/color/tone 创建与初值)等价。 |
| Viewport.dispose | 0x10010670 | 等价 | 原版幂等: 0x10099b60 出列表+释放, native=0, 不抛错。重建置标志等价。 |
| Viewport.flash | 0x10010bc0 | 桩 | 原版: color 可省, 非 nil 时类型检查(0x1017200c=Color)+RGBA 打包(0x10010c58-0x10010d07 逐字节 fistp)写 0x100181c0, 时长写 0x10017da0。重建 vp_flash(src:591) no-op。 |
| Viewport.update | 0x100106b0 | 桩 | 原版调 0x10017e10 推进 flash 计时(disposed 时 raise "disposed viewport")。重建 vp_update(src:592) no-op。 |
| Viewport.rect | 0x100106f0 | 等价 | 返回内部 Rect(0x10103f90="rect")。重建等价。 |
| Viewport.rect= | 0x10010730 | 偏差(详述) | 原版同 src_rect=: 不替换 ivar, 把参数 Rect 的 x,y,x+w,y+h 拷进内部 Rect(0x100107c7-0x100107f0)并写 native(虚表+0x2c 与 +0x8)。重建 vp_rect_set(src:559) 直接替换引用; 合成管线不用 viewport, rect 实际无效果(R1)。 |
| Viewport.visible | 0x10010b40 | 一致 | native+0x20 符号位。重建一致。 |
| Viewport.visible= | 0x10010800 | 等价 | RTEST → native(1/0), 返回归一化 true/false。重建返回原参数(轻)。 |
| Viewport.z | 0x10010b80 | 偏差(详述) | 原版 native+0x3c → **Integer**(0x1001d850)。重建 vp_z 返回 Float(src:585)。 |
| Viewport.z= | 0x10010860 | 等价 | native 虚表+0xc 直存, 无钳制。重建一致。 |
| Viewport.ox | 0x100108c0 | 偏差(详述) | 原版 native+0xa0 → **Integer**。重建返回 Float(src:581)。 |
| Viewport.ox= | 0x10010900 | 等价 | native 0x10018110 直存。重建一致。 |
| Viewport.oy | 0x10010960 | 偏差(详述) | 原版 native+0xa4 → **Integer**。重建 Float。 |
| Viewport.oy= | 0x100109a0 | 等价 | native 0x10018130 直存。重建一致。 |
| Viewport.color | 0x10010a00 | 一致 | 返回 ivar "color"(Color(0,0,0,0))。重建懒创建 (0,0,0,0)(src:565) 值等价。 |
| Viewport.color= | 0x10010d20 | 等价 | 原版类型检查+RGBA 逐字节截断打包(0x10010d7e-0x10010dfe fistp)写 native(0x100181c0)+同步内部 Color; 重建存引用。备注: viewport 色叠加未实现(R1)。 |
| Viewport.tone | 0x10010a40 | 一致 | 返回 ivar "tone"(Tone(0,0,0,0))。重建等价。 |
| Viewport.tone= | 0x10010a80 | 等价 | 原版同步 4 double 到内部 Tone(0x10010b17-0x10010b30)并分两笔写 native(0x10018150/0x10018180); 重建存引用。备注: tone 未实现(R1)。 |

## 四、渲染管线 / 主循环支撑逻辑

| 编号 | 项 | 判定 | 证据 |
|---|---|---|---|
| R1 | Viewport 合成 | 偏差 | 原版 sprite 按所属 viewport 裁剪并按 viewport z 分层(0x10007fb0 转场、0x10097350 场景绘制均经屏幕对象); 重建 rgss_compose_scene(src:1485-1525) 全局 z 排序一次画完, 完全不读 SpriteVal.viewport/ViewportVal.rect — viewport 裁剪、viewport z、viewport color/tone 全部缺失。 |
| R2 | 后缓冲 | 等价 | 重建 rgss_render.c: DIB 32bpp top-down(src:44-51)+BitBlt 呈现(src:71), 与原版 544x416 屏幕对象+呈现(0x10097350)语义对应。 |
| R3 | Sprite 特效合成 | 偏差 | 重建 blit 只实现 缩放(zoom)+alpha+blend_type 0/1/2(src:98-161); angle(旋转)、mirror、bush_depth/bush_opacity、wave_*、color 叠加、tone 色调、flash 全部不参与合成(属性有存储、无渲染)。原版这些效果在 native 绘制路径中实现(本审计未逐条反汇编绘制内核, 以 API 层可见性为准)。 |
| R4 | 绘制顺序 | 等价 | 重建 sort_sprites 按 z 升序稳定插入排序(src:1193-1204); 原版 z 排序+viewport 分层(R1 除外)一致。 |
| R5 | 帧率控制 | 等价 | 原版 0x10001640/0x10001890: timeGetTime 预算 1000/frame_rate, 重试≤4 次, 渲染耗时≥100ms 重置基线; 重建 QPC+Sleep/忙等(src:67-89)。限帧语义等价; 差异备注: 重建无 3ms 定时器精度调用、无 FPS 统计字段。 |
| R6 | F12 Reset | 等价 | 原版 0x100014a0: GetAsyncKeyState(0x7b) → intern("Reset") → 构造 Reset 异常 raise; 重建 wndproc WM_KEYDOWN F12 置标志(gamemain src:36-38)+load_and_run_scripts 捕获 "Reset" 异常重启(gamemain src:188-192)。机制不同, 重启行为等价。 |
| R7 | 诊断日志 | 偏差(无行为影响) | 重建 gfx_update/input/window 等处写 rgss_render.log / rgss_input.log / rgss_window.log / rgss_caller.log(src:95-110, 331-357, 795-798)。原版无对应行为; 不改变游戏语义但产生文件 IO 副作用。 |

## 五、共性偏差(适用于两类所有条目)

1. **dispose 后访问器不抛错**: 原版所有含 native 守卫的 getter/setter 在 dispose 后
   `rb_raise(RGSSError, "disposed sprite"/"disposed viewport")`(0x101048a8/0x10104a70);
   重建 Data 结构只有 disposed 标志, 访问器照常工作, dispose 亦不拒绝二次调用
   (原版 dispose 本身幂等不抛, 但后续访问抛)。
2. **类型差异**: 原版 x/y/z/ox/oy/opacity/bush_*/wave_*/Viewport z/ox/oy getter 均经
   0x1001d850 返回 Integer; 重建返回 Float(zoom/angle 原版本就 Float, 无差异)。
3. **owner/observer 回写链接缺失**: 原版 src_rect/rect/color/tone 与宿主间有回写
   (Rect.set → :src_rect=); 重建靠合成时直读对象实现视觉等价, 但对象标识语义与
   "旧 Rect 再 set 仍回写" 的边角行为不同。

## 六、偏差/桩/未映射清单(汇总)

**桩(8)**: Graphics.fadeout、Graphics.fadein、Graphics.freeze、Sprite.flash、Sprite.update、Viewport.flash、Viewport.update(7) + (Graphics.transition 计偏差)。
另 Sprite/Viewport flash+update 桩互为依赖: flash 状态无 update 推进。

**未映射(4)**: Graphics.width、Graphics.height、Graphics.visible、Graphics.visible=、Sprite.width、Sprite.height — 计 6 个方法(TSV 行 4+2), 上表按 4 行(Graphics 2 + Sprite 2)统计为"未映射"行数, 方法数实为 6。

**偏差(24, 按严重度排序)**:
1. Graphics.transition — 只调一次 update, 无逐帧交叉淡入淡出(已申报)
2. Graphics.frame_reset — 清零 frame_count, 原版仅重置 FPS 计时基线(语义相反)
3. Graphics.frame_rate= — 无 [10,120] 钳制
4. Graphics.brightness= — 无钳制且无渲染效果(依赖它的 fade/transition 连带失效)
5. Graphics._reset — 语义错位: 清 frame_count, 原版是 frame_rate←60+brightness←255+转场复位
6. Graphics.snap_to_bitmap — 返回空白位图(原版返回屏幕快照)
7. Graphics.resize_screen — 无 [1,640]x[1,480] 钳制、无实际重设后缓冲
8. Sprite.clone / Sprite.dup / Viewport.clone / Viewport.dup — 原版 raise, 重建可克隆
9. Sprite.width / Sprite.height — 缺失(原版=src_rect 尺寸)
10. Sprite.opacity= — 无 [0,255] 钳制(blit 内 alpha 可溢出)
11. Sprite.color — 默认 Color alpha 255, 原版为 0
12. Sprite.src_rect= / Viewport.rect= — 替换引用 vs 原版拷贝进内部 Rect(对象标识差异)
13. Sprite.viewport= / R1 — viewport 裁剪/层次/色叠加/色调在合成中整体缺失
14. Sprite.x/y/z/ox/oy、Viewport.z/ox/oy — 返回 Float, 原版 Integer
15. Viewport.initialize — 0 参可用(默认 0,0,544,416), 原版强制 1 或 4 参
16. R7 — 多个诊断日志文件的副作用

**无法验证(6)**: Sprite.zoom_x/zoom_y/angle/wave_amp/wave_length/wave_speed/wave_phase 的
setter 及其默认值逐指令核对(原生绘制内核未展开; 已确认 getter 返回 Float、构造默认
zoom=1.0、bush_opacity=128、wave 计时 180/360, 重建 wave 默认为 0)。
