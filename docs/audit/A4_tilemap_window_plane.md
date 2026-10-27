# A4 — Tilemap / Window / Plane 与原版二进制逻辑一致性审计

- 审计日期: 2026-10-27
- 对比材料: `build/full_disasm.txt`(原版 objdump, 基址 0x10000000, 脱壳版),
  `docs/assets/rgss_ruby_api_map.tsv`(97 条), Hex-Rays 伪代码缓存
  `build/orig_pseudo/functions/0x1000b4xx-0x1000bexx.c`(Plane 21 个函数), 字符串/表格数据直接取自
  `unpacked/RGSS202E_unpacked.dll`(PE 节表: 第 1 节 raw==va, 故 .data VA 即文件偏移)。
- 重建源码: `src/rgss_graphics_classes.c`, `src/rgss_render.c`(只读审计, 未改动)。
- 方法: 逐条按 TSV 地址读原版反汇编/伪代码, 与重建实现对照。
- 原版通用约定(适用于全部条目):
  - `0x10026860(self,0x22)` = 对象守卫; 之后 `native==0 → rb_raise(0x10172024=RGSSError,
    "disposed window"/"disposed tilemap"/"disposed plane")`(Window 字符串 0x10104a98,
    Tilemap 0x101049d8, Plane 0x101047b0)。
  - ivar 名实测: 0x101047d0="viewport" 0x101047dc="bitmap" 0x101049ec="bitmaps"
    0x101049fc="map_data" 0x10104a08="flash_data" 0x10104a14="passages"
    0x10104ad0="windowskin" 0x10104ac4="contents" 0x10104aa8="cursor_rect"。
  - `0x1001d850` = INT2FIX 尾调用(整数属性 getter 全部走它, 返回 Fixnum)。
  - `0x10009dd0` = 类型校验(参数, 期望类), 失败 TypeError。
  - 三类的原生对象都是 C++ 对象 + 虚表: Window vtbl 0x101052e4 (大小 0x2B8),
    Tilemap 层对象 vtbl 0x1010527c, 由 `0x10010e60`+`0x1003d8a0` 挂入全局链表
    (Tilemap 0x1017b4e8, Window 0x1017b550, Plane 0x1017b4bc), 析构用链表遍历
    (0x1000f970 / 0x10012110)。

## 判定计数

| 判定 | Tilemap(24) | Window(43) | Plane(30) | 合计(97) |
|---|---|---|---|---|
| 一致 | 5 | 14 | 8 | 27 |
| 等价 | 12 | 11 | 13 | 36 |
| 偏差(详述) | 2 | 13 | 5 | 20 |
| 桩 | 3 | 3 | 2 | 8 |
| 未映射(重建缺失) | 2 | 2 | 2 | 6 |
| 无法验证 | 0 | 0 | 0 | 0 |

- "一致" = 值语义与校验逐条对得上; "等价" = 行为等价但实现路径不同(ivar vs native 字段);
  "偏差" = 有可观察行为差异; "桩" = 重建为空实现; "未映射" = 原版有、重建完全缺失。

---

## Tilemap (24 条)

| class.method | 原版地址 | 判定 | 证据摘要 |
|---|---|---|---|
| Tilemap.alloc_func | 0x1000fae0 | 等价 | 原生 0xC 字节壳 + 链表节点(0x1003f5a0/0x1003f790/0x10010e60), 内部 NxTilemap 构造 0x10017000; 重建 calloc+注册(src/rgss_graphics_classes.c:664,725) |
| Tilemap.clone | 0x10008fb0 | 未映射 | 三类共享; 重建未覆盖 clone, Ruby 默认 clone 会共享 DATA_PTR(与原版深复制原生侧语义不同) |
| Tilemap.dup | 0x10008fd0 | 未映射 | 同上 |
| Tilemap.initialize | 0x1000f0d0 | 等价 | 参数 viewport 可选(0x101047d0); 构造 NxTilemap 0x10017000: 布局 0xCC 字节×2, 初始 visible=1(`10017126: mov [esi+0x1005C],ebx` ebx=1), ox=oy=0(+0x10060/+0x10064=0), **无 x/y/z/opacity/blend_type 字段**; 循环 9 次建 bitmaps 数组(`1000f181-1000f19d`, cmp esi,9); 重建一致并补 map_data/flash_data/passages=nil(src:666-676) |
| Tilemap.dispose | 0x100113f0 | 桩 | 原版: native 虚表[0](1) 释放 + native=0; 重建 no-op(src:692), disposed? 恒 Qfalse |
| Tilemap.disposed? | 0x10011420 | 桩 | 原版: `neg/sbb/and 2/add 2` → native==0 时 Qtrue; 重建恒 Qfalse(src:693) |
| Tilemap.update | 0x1000f1f0 | 桩 | 原版守卫后 `call 0x10016d50` 推进 A1 水波动画帧; 重建 no-op(src:694) → 水面静止 |
| Tilemap.viewport | 0x1000f230 | 等价 | ivar get 0x1008bf70; 重建同(src:690) |
| Tilemap.viewport= | 0x1000f270 | 等价 | 类型校验+ivar+native 同步 0x10016b90; 重建存 ivar(视口裁剪本身未实现, 见汇总#14)(src:691) |
| Tilemap.bitmaps | 0x1000f320 | 一致 | 返回 initialize 时建的 9 元素数组; 重建懒建 9×nil 数组(src:698-707), 观察等价 |
| Tilemap.[] | 0x1000f340 | 一致 | `cmp eax,8 / ja → IndexError`(`1000f399`, 格式 0x101049f4 "wrong index..."); 重建经 Table#[] 自然越界抛错, 语义一致 |
| Tilemap.[]= | 0x1000f3f0 | 一致 | 同样 0..8 校验 + native 同步 0x10016d60; 重建存 Table ✓ |
| Tilemap.map_data | 0x1000f500 | 等价 | ivar(0x101049fc) |
| Tilemap.map_data= | 0x1000f540 | 等价 | 校验 Table(0x1017b4dc)+native 尺寸同步 0x10016d80(`1000f5a6: mov edx,[esi+0x14]` 等 4 个维度); 重建存 VALUE, 绘制时经 Ruby [] 读取 ✓ |
| Tilemap.flash_data | 0x1000f5d0 | 等价 | ivar(0x10104a08); 闪烁效果未实现(汇总#14) |
| Tilemap.flash_data= | 0x1000f610 | 等价 | nil 时 native 清零(`1000f656: push 1,1,1,0 → 0x10016db0`), 否则同步; 重建存 VALUE ✓ |
| Tilemap.passages | 0x1000f6d0 | 等价 | ivar(0x10104a14) |
| Tilemap.passages= | 0x1000f710 | 等价 | 校验+native short[] 同步 0x10016de0; 重建存 ✓ |
| Tilemap.visible | 0x1000f790 | 一致 | `mov eax,[eax+0x1005C]; neg/sbb/and 2` bool→Qtrue/Qfalse; 重建同(src:688) |
| Tilemap.visible= | 0x1000f7d0 | 一致 | arg==Qnil/Qfalse→0 否则 1(`1000f7fa: cmp eax,4 / test eax,eax → 0x10016e00(0/1)`); 重建 RTEST ✓ |
| Tilemap.ox | 0x1000f830 | 偏差 | 原版 `mov eax,[eax+0x10060]; jmp 0x1001d850` → **Fixnum**; 重建返回 Float(src:683) |
| Tilemap.ox= | 0x1000f870 | 等价 | `sar eax,1`(Fixnum)或 0x100567D0 转换 → 0x10016e20(int); 重建 NUM2DBL, 整数值下行为一致, 渲染取 (int)ox ✓ |
| Tilemap.oy | 0x1000f8d0 | 偏差 | 同 ox: 原版 Fixnum(+0x10064), 重建 Float(src:684) |
| Tilemap.oy= | 0x1000f910 | 等价 | 0x10016e40; 同 ox= |

## Window (43 条)

原版 Window 方法一律守卫后转发 NxNativeWindow 虚表方法。原生层关键地址:
构造 0x10019680(经 0x10015ad0 分配 4 个 0x2B8 子对象 + 3 个 0xC4), 绘制 0x10019000,
vp= 0x100183c0, update 0x100183f0, z= 0x10018590, active= 0x100185b0, visible= 0x100185e0,
pause= 0x10018630, contents= 0x10018de0, ox= 0x10018e00, oy= 0x10018e20,
opacity= 0x10018a50, back_opacity= 0x10018b00, contents_opacity= 0x10018b80,
x= 0x10018f90, y= 0x10018fb0, openness= 0x10018fd0, windowskin= 0x10019520,
width= 0x10019540, height= 0x10019550, cursor_rect 同步 0x10018bb0。

| class.method | 原版地址 | 判定 | 证据摘要 |
|---|---|---|---|
| Window.alloc_func | 0x10012270 | 等价 | 同 Tilemap 模式; 重建 calloc+注册(src:749,755) |
| Window.clone | 0x10008fb0 | 未映射 | 同 Tilemap.clone |
| Window.dup | 0x10008fd0 | 未映射 | 同上 |
| Window.initialize | 0x100112b0 | 偏差(详述) | 原生构造 0x10019680 初始值: opacity/back_opacity/contents_opacity/openness 全 255(`10019940: mov eax,0xff` → +0x60/+0x64/+0x68/+0x6c), active=1(`10019980: [esi+0x80]=1`), visible=1(`1001998a: [esi+0x84]=1`), pause=0, x=y=0, width=height=0, **无 contents 预分配、无 cursor_rect 预分配(初值 nil)**。重建(src:769-800): ① eager 创建 contents=Bitmap(w,h)(src:789-790) — 原版 contents 初值 nil, 依赖 `contents.nil?` 的 Window_Base 语义被破坏; ② eager 创建 cursor_rect=Rect(0,0,0,0)(src:791-792); ③ **back_opacity 初值 192**(src:782) vs 原版 255 — 默认窗体背景更透。active=1/visible=1/openness=255 一致 |
| Window.dispose | 0x100113f0 | 桩 | 原版 vt[0](1)+native=0; 重建 no-op(src:869), 且不注销 g_windows → 已 dispose 窗口仍被绘制 |
| Window.disposed? | 0x10011420 | 桩 | 重建恒 Qfalse(src:870) |
| Window.update | 0x10011450 | 桩 | 原版 `call 0x100183f0(1)`(光标闪烁帧等内部状态推进); 重建 no-op(src:871) |
| Window.viewport | 0x10011490 | 等价 | ivar |
| Window.viewport= | 0x100114d0 | 等价 | 校验+vt 0x100183c0; 重建存 ivar(视口裁剪未实现, 汇总#14) |
| Window.windowskin | 0x10011580 | 等价 | ivar(0x10104ad0) |
| Window.windowskin= | 0x100115c0 | 等价 | 校验 Bitmap(0x10171ffc)+vt 0x10019520(0); 重建存+下一帧重绘 ✓ |
| Window.contents | 0x10011660 | 偏差 | 原版返回 ivar(初值 nil); 重建因 initialize eager 分配永远非 nil(随 initialize#①) |
| Window.contents= | 0x100116a0 | 等价 | 允许 nil(0x100116cf cmp esi,4 → vt 0x10018de0(0)); 重建 ✓ |
| Window.cursor_rect | 0x10011740 | 偏差 | 原版初值 nil; 重建初值 Rect(0,0,0,0)(随 initialize#②) |
| Window.cursor_rect= | 0x10011780 | 等价 | 校验 Rect(0x1017b4c8)+把 x/y/w/h 值拷回传入 Rect 并同步 native x+w,y+h(0x100117f1 `add ecx,[esi+8]` → 0x10018bb0); 重建只存 VALUE, 值一致无观察差异 |
| Window.active | 0x10011840 | 一致 | `[native+0x80]` bool; 重建同(src:859) |
| Window.active= | 0x10011880 | 一致 | vt 0x100185b0(0/1); 重建 RTEST ✓ |
| Window.visible | 0x100118e0 | 一致 | `[+0x84]`; 重建同(src:855) |
| Window.visible= | 0x10011920 | 一致 | vt 0x100185e0; 重建 ✓ |
| Window.pause | 0x10011980 | 一致 | `[+0x88]`; 重建同(src:857)。(pause 箭头绘制未实现, 汇总#14) |
| Window.pause= | 0x100119c0 | 一致 | vt 0x10018630; 重建 ✓ |
| Window.x | 0x10011a20 | 偏差 | `[+0x44]; jmp 0x1001d850` → **Fixnum**; 重建返回 Float(src:804) |
| Window.x= | 0x10011a60 | 等价 | int → vt 0x10018f90; 重建 NUM2DBL, 整数一致 |
| Window.y | 0x10011ac0 | 偏差 | `[+0x48]` Fixnum; 重建 Float(src:805) |
| Window.y= | 0x10011b00 | 等价 | vt 0x10018fb0 |
| Window.width | 0x10011b60 | 一致 | Fixnum; 重建 INT2FIX(src:839) ✓ |
| Window.width= | 0x10011ba0 | 等价 | vt 0x10019540, **无钳制**; 重建无钳制 ✓(src:840) |
| Window.height | 0x10011c00 | 一致 | 同 width |
| Window.height= | 0x10011c40 | 等价 | vt 0x10019550, 无钳制 ✓ |
| Window.z | 0x10011ca0 | 偏差 | Fixnum; 重建 Float(src:806), 且合成管线按创建序画窗、忽略 z(见 z=) |
| Window.z= | 0x10011ce0 | 偏差 | 原版 vt 0x10018590 参与 z 重排; 重建只存值, rgss_render.c:1547 按创建序绘制 Window, 无排序 |
| Window.ox | 0x10011d40 | 偏差 | `[+0x54]` Fixnum; 重建 Float(src:807) |
| Window.ox= | 0x10011d80 | 等价 | vt 0x10018e00; 重建存+渲染用 (int)ox 滚动 contents(src/render:1567) ✓ |
| Window.oy | 0x10011de0 | 偏差 | `[+0x58]` Fixnum; 重建 Float(src:808) |
| Window.oy= | 0x10011e20 | 等价 | vt 0x10018e20; 同 ox= |
| Window.opacity | 0x10011e80 | 一致 | `[+0x60]` Fixnum; 重建 INT2FIX(src:830) ✓ |
| Window.opacity= | 0x10011ec0 | 一致 | vt 0x10018a50: `jge/`cmp 0xff` 钳制 [0,255](`10018a5f-10018a6c`)再写 +0x60 并重算; 重建钳制 [0,255](src:831-838) ✓ |
| Window.back_opacity | 0x10011f20 | 偏差 | `[+0x64]` Fixnum; 重建 Float(src:811) 且初值 192(随 initialize#③) |
| Window.back_opacity= | 0x10011f60 | 偏差 | vt 0x10018b00 有 `cmp 0xff/jge` **钳制 [0,255]**; 重建 WIN_ACC 直存 NUM2DBL **无钳制**(src:811) |
| Window.contents_opacity | 0x10011fc0 | 偏差 | `[+0x68]` Fixnum; 重建 Float(src:812) |
| Window.contents_opacity= | 0x10012000 | 偏差 | vt 0x10018b80 钳制 [0,255]; 重建无钳制(src:812) |
| Window.openness | 0x10012060 | 一致 | `[+0x6c]` Fixnum; 重建 INT2FIX(src:817-820) ✓ |
| Window.openness= | 0x100120a0 | 一致 | vt 0x10018fd0: `test eax,eax/jge` + `cmp eax,0xff/jle` → **钳制 [0,255]**(`10018fd4-10018ff1`)再写 +0x6c、call 0x10018e40 重算; 重建钳制 0..255(src:821-828) ✓ (渲染端 openness→几何的联动见专项) |

## Plane (30 条; 伪代码缓存覆盖全部)

| class.method | 原版地址 | 判定 | 证据摘要 |
|---|---|---|---|
| Plane.alloc_func | 0x1000c0e0 | 等价 | 0xC 壳+链表(0x1017b4bc); 重建 calloc(src:608), 但未注册进合成(汇总#12) |
| Plane.clone | 0x10008fb0 | 未映射 | 同 Tilemap.clone |
| Plane.dup | 0x10008fd0 | 未映射 | 同上 |
| Plane.initialize | 0x1000b420 | 等价 | 伪代码: viewport 可选; `sub_10006B20` 建 Color、`sub_100103E0` 建 Tone 并挂 owner/observer(0x10103e58/0x10103e74, 与 Sprite 同机制 — 重建未挂 observer, Color/Tone #= 不会联动, 值语义仍等价); visible/opacity/zoom 初值经 native 构造 0x10014460; 重建显式初值一致(src:610-622) |
| Plane.dispose | 0x100113f0 | 桩 | 同 Tilemap.dispose |
| Plane.disposed? | 0x10011420 | 桩 | 同 Tilemap.disposed? |
| Plane.viewport | 0x1000b5b0 | 等价 | ivar |
| Plane.viewport= | 0x1000b5f0 | 等价 | 校验+vt+28; 重建存 ivar |
| Plane.bitmap | 0x1000b570 | 等价 | ivar(0x101047dc) |
| Plane.bitmap= | 0x1000b6a0 | 等价 | 校验+vt+44(bitmap,0); 重建存(绘制未实现, 汇总#12) |
| Plane.visible | 0x1000b750 | 一致 | `*(native+244)!=0 ? Qtrue : Qfalse`; 重建同(src:635) |
| Plane.visible= | 0x1000b790 | 等价 | vt+4(0/1); 重建 RTEST ✓ |
| Plane.z | 0x1000bbf0 | 偏差 | `[native+0x3C]; jmp 0x1001d850` → **Fixnum**; 重建返回 Float(src:628) |
| Plane.z= | 0x1000b7f0 | 偏差 | int(sar 1 / 0x100567D0)→vt+0xC; 重建 NUM2DBL(src:628); 且 Plane 未参与任何排序/绘制 |
| Plane.ox | 0x1000b850 | 偏差 | `[native+0xC4]` Fixnum; 重建 Float(src:629) |
| Plane.ox= | 0x1000b890 | 等价 | int → 0x10013ED0; 重建 NUM2DBL |
| Plane.oy | 0x1000b8f0 | 偏差 | `[native+0xC8]` Fixnum; 重建 Float(src:630) |
| Plane.oy= | 0x1000b930 | 等价 | int → 0x10013EF0 |
| Plane.zoom_x | 0x1000b990 | 一致 | `fld [native+0xD0]` double → rb_float_new(0x100552C0); 重建 rb_float_new(src:631) ✓ |
| Plane.zoom_x= | 0x1000b9d0 | 一致 | 0x1005A2A0 → double → 0x10013F50; 重建 NUM2DBL ✓ |
| Plane.zoom_y | 0x1000ba20 | 一致 | `fld [native+0xD8]`; 重建 ✓ |
| Plane.zoom_y= | 0x1000ba60 | 一致 | → 0x10013F40; 重建 ✓ |
| Plane.opacity | 0x1000bc30 | 一致 | 原生色块+140 处第 13 字段(int) → Fixnum; 重建 INT2FIX(src:633) ✓ |
| Plane.opacity= | 0x1000bc90 | 一致 | `jge/cmp 0xff` **钳制 [0,255]**(`1000bcd9-1000bcdb`)写回; 重建钳制(src:633 宏? — PLN_ACC opacity 用 NUM2INT 无钳制!见偏差) |
| Plane.blend_type | 0x1000bd20 | 一致 | `v3[0]&7`(低 3 位打包) → Fixnum; 值域 0..2 一致 |
| Plane.blend_type= | 0x1000bd80 | 偏差 | 原版先 `sub_10008F40(v,0,2,"blend_type")` **RangeError 校验 0..2**; 重建 PLN_ACC 直存无校验(src:634) |
| Plane.color | 0x1000bab0 | 等价 | ivar Color |
| Plane.color= | 0x1000be30 | 等价 | 校验 Color(0x1017200C)+rgba 拷入传入 Color(v4[1..4]=v2[1..4])+native 0x10013FC0; 重建存 VALUE(着色效果未实现, 汇总#14) |
| Plane.tone | 0x1000baf0 | 等价 | ivar Tone |
| Plane.tone= | 0x1000bb30 | 等价 | 校验 Tone(0x1017B4F4)+double→long 钳位(0x100DC7D0)+native 0x10013F70/0x10013FA0; 重建存 VALUE(色调未实现) |

> 更正: Plane.opacity= 原版钳制 [0,255], 重建 PLN_ACC(src:633) 用 NUM2INT **无钳制** — 应记偏差。
> 上表该行判定按"一致"归档有误, 修正为偏差: **Plane.opacity= 偏差(缺钳制)**, 计数相应调整为
> Plane: 一致 7 / 等价 13 / 偏差 6, 全表: 一致 26 / 等价 36 / 偏差 21 / 桩 8 / 未映射 6 / 无法验证 0。

---

## 专项 1 — Tilemap autotile「48 形状 → 2x3 块 6 片」映射证据(供开发会话还原)

原版把每张自动图块素材按 **2x3 块(64x96, 6 片 32x32, piece p → 列 p%2 行 p/2)** 使用,
48 种连接形状通过三张 **48 项 × 8 字节** 静态表查得每个 2x2 象限(TL/TR/BL/BR)应取的 piece 索引。

### 表地址(.data, 文件偏移=VA, 已逐字节提取验证)

| 表 | VA | 用途 | 有效形状数 |
|---|---|---|---|
| T1 | 0x10104df8 | A1(水面/海滩, 48 全有效; 表尾含 0x10104dfc/0x10104dfd 的逐形状 flag) | 48 |
| T2 | 0x10104f78 | A2(地板 0-15, 墙格式按列奇偶另行展开) | 16 |
| T3 | 0x101050f8 | A3(0-7) | 8 |

三表首址间隔恰为 0x180(384B = 48×8)。

### 查表代码(绘制函数 0x100172e0 内, 循环 4 个 2x2 象限)

- 表选择: `1001765c: mov eax,0x10104df8` / `10017666: mov eax,0x10104f78` /
  `1001766d: mov eax,0x101050f8`(由 [esp+0x18] 的 0/1/其他 = 墙/地板格式标志选择)。
- 取形状行: `10017676: lea eax,[eax+ecx*8]`(基址 + shape×8)。
- 象限取一对 piece 索引: `10017694: movzx ebx,BYTE PTR [eax+0x1]` /
  `10017698: movzx edi,BYTE PTR [eax]`。
- 象限行列: `100176a6: div ecx`(ecx=2)。
- piece→像素偏移: `100176aa: shl ecx,0x4`、`100176ba: shl eax,0x4`(×16, 半片 16px 粒度)。
- 子块绘制: `100177b2(及 10017771): call 0x10096680`; 循环控制 `100177c3: cmp eax,4 /
  100177ca: jl 0x10017690`。
- 水面半格特例: `10017701: cmp ebx,0x1 / 10017706: cmp ebx,0x5` 命中时
  `10017776: add [esp+0x20],8`(右列 +8px)。
- A1 水波动画帧判定辅助: **0x100171a0**(id∈[0x610,0x7FF] 或 A1 海滩区且 `(id-0x800)/48 mod 8 != 7`),
  **0x10017200**(同式 mod 8 == 7); 帧号计算 `100177e8: add eax,0xfffff800; idiv 0x30;
  …ebp=(frame/8)-2 ∈[0,3]`、`10017819: and ebx,7`(列)。
- 形状来源: 层函数以四邻同 kind 连接性组合出 shape(0..47), 与重建的推导方向相同,
  但原版**查表**而重建**近似**: src/rgss_graphics_classes.c:1266-1294 的 draw_autotile 只用
  piece 0..4 五种自拼(缺 piece5, 且 shape 仅 ==47 特判, 未查表)。

### 表内容(每形状 4 对 piece 索引, 顺序 TL TR BL BR; `xy` = 第 x 列 y 行片)

T1(0x10104df8, A1/水面, 全 48 形状):
```
shape  0- 7: 24 14 23 13 | 20 14 23 13 | 24 30 23 13 | 20 30 23 13
             24 14 23 31 | 20 14 23 31 | 24 30 23 31 | 20 30 23 31
shape  8-15: 24 14 21 13 | 20 14 21 13 | 24 30 21 13 | 20 30 21 13
             24 14 21 31 | 20 14 21 31 | 24 30 21 31 | 20 30 21 31
shape 16-23: 04 14 03 13 | 04 30 03 13 | 04 14 03 31 | 04 30 03 31
             22 12 23 13 | 22 12 23 31 | 22 12 21 13 | 22 12 21 31
shape 24-31: 24 34 23 33 | 24 34 21 33 | 20 34 23 33 | 20 34 21 33
             24 14 25 15 | 20 14 25 15 | 24 30 25 15 | 20 30 25 15
shape 32-39: 04 34 03 33 | 22 12 25 15 | 02 12 03 13 | 02 12 03 31
             22 32 23 33 | 22 32 21 33 | 24 34 25 35 | 20 34 25 35
shape 40-47: 04 14 05 15 | 04 30 05 15 | 02 32 03 33 | 02 12 05 15
             04 34 05 35 | 22 32 25 35 | 02 32 05 35 | 00 10 01 11
```
(如 shape47 = 00 10 01 11 = 四象限分别取 piece 0,1,2,3 — 全连接的基本 2x2。)

T2(0x10104f78, A2): shape 0-15 =
```
22 12 21 11 | 02 12 01 11 | 20 10 21 11 | 00 10 01 11
22 32 21 31 | 02 32 01 31 | 20 30 21 31 | 00 30 01 31
22 12 23 13 | 02 12 03 13 | 20 10 23 13 | 00 10 03 13
22 32 23 33 | 02 32 03 33 | 20 30 23 33 | 00 30 03 33
```
shape 16-47 全 0(由墙格式路径按列奇偶展开, 见 0x10017601-0x10017637 的
`lea ecx,[ebx+ebx*4-0x32]; shl ecx,4` 等几何计算, 非查表)。

T3(0x101050f8, A3): shape 0-7 =
```
20 10 21 11 | 00 10 01 11 | 20 30 21 31 | 00 30 01 31
20 10 23 13 | 00 10 03 13 | 20 30 23 33 | 00 30 03 33
```
shape 8-47 全 0。

## 专项 2 — Window 九宫格皮肤 / openness / contents 坐标核对

- 皮肤绘制入口: **0x10019000**(NxNativeWindow::draw 系)。背景/边框/光标几何常量:
  `1001906f: push 0x20`(32)、`10019122: mov ebp,0x40`(64)、`100191cd/1001920e/1001924e/
  100192f0/1001931e: push 0x30`(48) — 与 128x128 皮肤(背景 64、光标 32、内边 48 一带)布局吻合;
  `100191e3 / 1001926d: add eax,0xfffffff0`(**-16**)与 contents 内缩 16px 一致。
  重建的切法(背景 (0,0,64,64) 拉伸 + 边框 (64,0,64,64) 9 宫格 6px 边(src:1340-1347,1406-1421))
  与这些常量兼容, 但**逐像素等价性无法在本审计中验证**(原版内部还有 0x1009A280 色块合成与
  条纹/渐变参数), 记: 渲染效果无法验证, 结构级一致。
- openness: 数值钳制已确认一致(0x10018fd0); 渲染端 openness= 钳制后 call 0x10018e40
  重算绘制参数, 重建用垂直居中压缩 h_eff=wh*openness/255(src:1434-1436)近似 —
  常量旁证存在(0x10019000 区间的 0x20/0x30/0x40 与居中计算), 逐像素无法验证。
- contents 坐标: 重建 (+16,+16) 且随 ox/oy 滚动(src:1567) — 与 0x10019000 内 -16 内缩及
  0x10018E00/0x10018E20(ox/oy 写入)方向一致; 无反证。
- 光标: cursor_rect 为 contents 相对坐标(重建 +16 偏移后绘制 src:1479-1480 ✓);
  闪烁: 重建用 g_frame_count%40 三角波 128..255(src:1475-1477)近似原版
  update 0x100183f0 推进的内部计数器 — 周期/振幅无法从静态反汇编直接证实, 无法验证。
- 原版 opacity/back_opacity/contents_opacity 三值在 0x10018a50/0x10018b00 内相乘
  (`imul ecx,[ebx+0x60]` × `imul eax`/`[edx+0x64]`, 除 255 用魔数 0x80808081)分别作用于
  背景/边框/内容色块 — 重建 draw_window_skin 的 bop/fop/cop 组合(src:1440-1478)与之同构。

---

## 汇总: 偏差/桩/未映射清单(21 偏差 + 8 桩 + 6 未映射)

渲染级共性问题(跨类, 影响下述多条判定):
(12) **Plane 完全未参与合成** — rgss_render.c:1485-1573 只画 Tilemap/Sprite/Window, Plane 无绘制路径;
(13) **合成不按 z 排序 Window/Plane**, Tilemap 固定最先、Window 按创建序(src:1489-1547) vs 原版 z 重排 0x10018590;
(14) viewport 裁剪、tone/color/flash_data 着色效果、Tilemap 闪烁、pause 箭头均未实现(与 A3 报告共享的渲染缺口)。

逐条偏差:
1. **Window.initialize** 偏差: contents/cursor_rect 预创建(原版 nil, 破坏 `contents.nil?` 语义); back_opacity 初值 192 vs 原版 255(0x10019940 区) — src:769-800。
2. **Window.back_opacity=** 偏差: 无 [0,255] 钳制 vs 0x10018b00 — src:811。
3. **Window.contents_opacity=** 偏差: 无钳制 vs 0x10018b80 — src:812。
4. **Window.opacity=(附带)**: 重建返回 INT2FIX(n) 而原版返回实参 — src:831-838(轻微)。
5. **Window.x/y/z/ox/oy/back_opacity/contents_opacity getter** 偏差: 返回 Float, 原版 Fixnum(0x1001d850 尾调用) — src:804-812。
6. **Window.z / z=** 偏差: 不参与排序(见共性#13) — src:806,1111-1112。
7. **Plane.opacity=** 偏差: 无 [0,255] 钳制 vs 0x1000bc90(`cmp 0xff`) — src:633。
8. **Plane.blend_type=** 偏差: 无 0..2 RangeError 校验 vs 0x1000bd80(`sub_10008F40(v,0,2)`) — src:634。
9. **Plane.z/ox/oy getter** 偏差: Float vs Fixnum — src:628-630。
10. **Tilemap.ox/oy getter** 偏差: Float vs Fixnum(0x1000f830/0x1000f8d0 → 0x1001d850) — src:683-684。
11. **Tilemap autotile 绘制** 偏差(已知申报): 四邻近似 vs 48 形状表(专项 1), 缺 piece5/shape 查表/A1 墙格式列展开/水面半格 +8px 特例 — src:1266-1294。

桩(重建空实现):
12. **Tilemap.dispose/disposed?**(0x100113f0/0x10011420) — src:692-693。
13. **Tilemap.update**(0x1000f1f0→0x10016d50 A1 水波推进) — src:694; 连带重建渲染无水波动画。
14. **Window.dispose/disposed?/update**(0x100113f0/0x10011420/0x10011450→0x100183f0) — src:869-871; dispose 不注销 g_windows(绘制侧亦未检查 disposed)。
15. **Plane.dispose/disposed?** — src:645-646。

未映射(原版有、重建缺失):
16. **clone/dup**(三类共享 0x10008fb0/0x10008fd0): 重建未覆盖, 默认 clone 共享 DATA_PTR, 存在 double-free/状态共享风险。

重建多写(TSV 无此方法, 属超集; 原版原生对象无对应字段):
17. **Tilemap x/y/z/opacity/blend_type**(src:680-686,1071-1084) — 原版 Tilemap 无这些访问器(NxTilemap 无对应字段, initialize 反汇编 0x100170b0-0x10017144 全字段清点可证)。
18. **Window zoom_x/zoom_y**(src:809-810,1117-1120) — 原版 Window 无 zoom(0x10019680 构造全字段清点可证; TSV 亦无)。

重建漏写(相对原版可观察面): 无 — TSV 97 条全部在重建中有对应方法(clone/dup 除外), 无缺失注册。
