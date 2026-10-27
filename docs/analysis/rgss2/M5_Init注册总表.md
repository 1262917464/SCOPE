# M5-b — RGSS 全部 14 类 Init 注册函数总表
> 注：本文写于 2026-10-27 仓库重构前；文中 `backup/` `unpacked/` `build/` `test/` `third_party/` 等路径现位于 `local/` 下，`src/rgss_*.c` 现为 `src/runtimes/rgss2/*.c`，`tools/` 已按流水线分桶（unpack/pe/analysis/probe/verify）。


日期：2026-10-26 ｜ 方法：objdump 全量反汇编离线扫描（`tools/find_init_fns.py`），
以 `rb_define_class=0x10020810 / rb_define_module=0x10020AD0 / rb_define_method=0x10021070 /
rb_define_singleton_method=0x10021250 / rb_define_alloc_func=0x10027310` 的 push 类名/模块名
实参为锚，向上回溯 int3 padding 定位函数边界。Audio 一行与 Hex-Rays 反编译交叉验证吻合。

## Ruby C API 密码本（本里程碑新增，后续逆向直接引用）

| 地址 | 函数 |
|------|------|
| 0x10020810 | rb_define_class(name, super) |
| 0x10020AD0 | rb_define_module(name) |
| 0x10021070 | rb_define_method(klass, name, fn, argc) |
| 0x10021250 | rb_define_singleton_method(klass, name, fn, argc) |
| 0x10027310 | rb_define_alloc_func(klass, fn) |
| 0x100330A0 | eval 原语（rb_eval_string_protect 风格） |
| 0x10008B00 / 0x10008B50 | eval 变体（带/不带异常捕获标志?） |
| 0x10054C70 | TYPE/断言类（返回值 7=T_STRING 10=T_ARRAY …，rb_type 语义） |
| 0x100808D0 | RSTRING(v)->ptr（字符串取内容） |
| 0x10080CA0 / 0x10080C10 | rb_str 替换/赋值族 |
| 0x100567D0 | NUM2INT 族 |

## Init 注册函数总表

| 类/模块 | Init 函数 | 类型 | 已知方法数（方法名区） |
|---------|-----------|------|---------------------|
| Audio | 0x10003B60 | module | 11（bgm/bgs/me/se × play/stop/fade）✅已反编译 |
| Bitmap | 0x10006060 | class | 20（blt/draw_text/hue_change/…） |
| Color | 0x100068C0* | class | 14（red/green/blue/alpha/set/_dump/_load/==/===/eql?）✅ |
| Font | 0x100075D0 | class | 22（name/size/bold/italic/shadow/color + default_*） |
| Graphics | 0x100084C0 | module | 15（update/freeze/transition/frame_*/brightness/…） |
| Input | 0x100087B0 | module | 5（press?/trigger?/repeat?/dir4/dir8） |
| Plane | 0x1000C150 | class | 15 |
| Rect | 0x1000C910 | class | 12（x/y/width/height/set/empty/_dump/_load）✅离线解码 |
| Sprite | 0x1000E260 | class | 32 |
| Table | 0x1000EF40* | class | 9（[]/[]=/resize/xsize/ysize/zsize/_dump/_load）✅ |
| Tilemap | 0x1000FB50 | class | 7（map_data/flash_data/passages/bitmaps） |
| Tone | 0x100101A0 | class | 14（red/green/blue/gray/set/_dump/_load/==/===/eql?）✅ |
| Viewport | 0x10011090 | class | 10 |
| Window | 0x100122E0 | class | 20 |

*注：Color/Table 的 Init 函数边界由调用链（sub_10009E60 的第 2/3 参）与离线扫描双重确认；
  Table 行的 init_fn 实为 alloc_func 注册块起点，Init 本体 = 0x1000EFA0（此前已反编译）。

## 附：此前批次已确认

- Init_Audio 11 方法地址全录（M5-a）
- Init_Table（sub_1000EFA0）：initialize/-1, initialize_copy/1, resize/-1, _dump/1, _load(s)/1, []/-1, []=/-1, xsize/0, ysize/0, zsize/0；alloc_func=0x1000E7F0
- Init_Color（sub_10006950）：initialize/-1, initialize_copy/1, set/-1, to_s/0, _dump/1, _load(s)/1, red,red=,green,green=,blue,blue=,alpha,alpha=, ==,===,eql?（各1）；alloc_func=0x1000FDD0（与 Tone 共享）
- Init_Tone（sub_10010210）：同 Color 骨架 + gray,gray=；==/===/eql? → 0x100101A0
- Init_Rect（0x1000C910 离线）：initialize(0x1000C480), initialize_copy(0x1000C500), set(0x1000C570), empty(0x1000C5A0), to_s(0x1000C5D0), _dump(0x1000C640), _load(0x1000C690), x(0x1000C6F0), y(0x1000C710), width(0x1000C760), height(0x1000C780), 以及 set 变体(0x1000C7D0/0x1000C7F0/0x1000C840)

## 全量 Ruby API 映射表（本里程碑产出）

`tools/decode_rbdefs.py` 机械解码 14 个 Init 函数内的全部注册调用，产出
**`docs/analysis/rgss2/rgss_ruby_api_map.tsv`（328 条，含 class/api/method/argc/fn_va/fn_rva）**。
解码规则（实测确认，cdecl 逆序 push）：
```
push argc      ; 可选, 无则默认 -1(可变参数)
push fn        ; C 实现函数 VA
push name      ; 方法名字符串 VA
push klass     ; 寄存器(ecx/edx/eax)
call rb_define_method(0x10021070) / rb_define_singleton_method(0x10021250)
```
校验：Audio.bgm_play→0x100035A0（与 Hex-Rays 一致）、argc=-1、Rect.initialize argc=4、
Sprite 注册 clone/dup（共享实现 0x8FB0/0x8FD0）——全部吻合。

| 类/模块 | 注册条数 | 类/模块 | 注册条数 |
|---------|---------|---------|---------|
| Audio | 11 | Table | 11 |
| Bitmap | 23 | Tilemap | 24 |
| Color | 18 | Tone | 18 |
| Font | 28 | Viewport | 21 |
| Graphics | 20 | Window | 43 |
| Input | 6 | Sprite | 56 |
| Plane | 30 | Rect | 19 |
| **合计** | | | **328** |

## 意义

1. 14 个 RGSS 类的**每个 Ruby 方法的 C 实现函数地址**现在都可以机械提取
   （Init 函数体 = push name / push fn / call rb_define_method 序列，objdump 即可全量解码，
   不再依赖 IDA 在线状态）。
2. 配合 M4 §3 方法名表，M5 剩余工作（逐类还原）变成纯流水线作业。
