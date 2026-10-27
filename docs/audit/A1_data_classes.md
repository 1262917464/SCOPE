# A1 — 数据类（Color / Tone / Rect / Table / Font）白盒一致性审计

- 审计日期：2026-10-27（只读审计，未改动任何 src/ 文件）
- 重建源码：`src/rgss_data_classes.c`
- 原版参考：`docs/assets/rgss_ruby_api_map.tsv` + `build/full_disasm.txt`（objdump，基址 0x10000000）
- 方法：按 TSV 的 fn_va 在 full_disasm.txt 中逐函数解码（数据类函数均不在 Hex-Rays 缓存内，全部读汇编），关键字符串/浮点常量已用 `backup/RGSS202E.dll` 原始字节核对。
- 判定枚举：一致 / 等价（实现不同但行为同）/ 偏差 / 桩 / 未映射 / 无法验证

## 公共辅助 thunk 语义（读汇编所得）

| 地址 | 语义 | 证据 |
|---|---|---|
| 0x10008f20 | `rb_scan_args` 风格的 argc 上下界检查（失败抛 ArgumentError） | Color#initialize 0x100063b3/0x100063c9 处 `(argc<3)||argc>4` 后调用 |
| 0x10008f40 | 数值范围检查 `(v<lo||v>hi)` 失败抛 "bad value for %s" | 缓存伪代码 0x10008f40.c |
| 0x10008f70 | 内部"修改通知"（owner/observer ivar 同步钩子，非 Ruby 公开 API） | 缓存伪代码 0x10008f70.c |
| 0x10009dd0 | 类型转换检查，失败抛 TypeError "can't convert %s into %s" | 缓存伪代码 0x10009dd0.c |
| 0x1005a2a0 | NUM2DBL | Color#initialize 全程经此转 double |
| 0x100567d0 | FIX2LONG（非 Fixnum 走此路径） | Rect#initialize 0x1000c495 |
| 0x1001d850 | FIXNUM 标记化（int→VALUE，溢出走 bignum） | Table#[] 0x1000ee15 |
| 0x100552c0 | double→rb_float_new | Color#red 0x10006796 |
| 0x10026860 | Data_Get_Struct（0x22=16 为安全等级参数） | 所有实例方法入口 |
| 0x1003f5a0 / 0x1003f790 | ruby_xmalloc / Data_Make_Struct alloc | 各 alloc_func |
| 0x100dc03f | ruby_xmalloc | Table#initialize 0x1000e922 |
| 0x100dc6e0 | xmemset | Table 清零缓冲 |
| 0x100de190 | xmemcpy | Table 拷贝数据 |

关键 Ruby 值编码（由 Init_Font 0x100079b0-0x10007a00 赋值确认）：Qfalse=0x0、Qtrue=0x2、Qnil=0x4、Fixnum n = 2n+1（0x29→size=20，0x3ff→float 255.0）。

## 原版数据布局

- Color/Tone：Data 指针指向 0x28 字节块，4 个 double：`+8 r, +0x10 g, +0x18 b, +0x20 a(Color)/gray(Tone)`（initialize_copy 0x100065eb 起四连 fld/fstp 证实）。
- Rect：0x18 字节块，4 个 long：`+8 x, +0xc y, +0x10 w, +0x14 h`（initialize_copy 0x1000c54b）。
- Table：0x20 字节块：`+8 argc, +0xc xsize, +0x10 ysize, +0x14 zsize, +0x18 元素数, +0x1c short*`（0x1000e90f-0x1000e933）。
- Font：0xc 字节块，单一 4 字节槽 +8（内部句柄）；所有属性通过 `0x1008bf70(ivar_get)/0x1008bf90(ivar_set)` 存 Ruby 对象。
- Color 与 Tone 的 initialize_dump/load/red/green/blue 及 == 共享实现（0x100063b0/0x10010030/0x100066c0/0x10010080/0x100100a0/0x10006780/0x100067a0/0x10006820/0x10006840/0x100068e0），差异仅在 alpha=（0x10010120）与 gray=（0x10010120 同址）。

## 逐条判定

### Color（15 条）

| class.method | 原版地址 | 判定 | 证据摘要 |
|---|---|---|---|
| Color.alloc_func | 0x1000fdd0 | 偏差 | 原版 alloc 指向 Tone 共享分配器 0x1000fdd0（Init_Color 0x10006969 `push 0x1000fdd0`）；仅 xmalloc+清零+Data_Make_Struct(0x1003f790)。重建 `color_alloc`（src:21）额外 `calloc(1, sizeof(ColorVal))` 隐式置零后包 Data_Wrap_Struct。分配层细节不同但结果均为全零 Data——见下方偏差说明 D0 |
| Color.initialize | 0x100063b0 | 偏差 | 原版 argc 检查 `3<=argc<=4`（0x100063b3/0x100063c9→0x10008f20）；4 参时 a 仍过 NUM2DBL（0x10006519-0x10006572），**无 255 默认值分支**（argc==4 路径在 0x1000650a `cmp edi,0x4; jne 0x1000657a` 后仅重走 a 的数值转换，无 fld 255.0）。a 省略时的初值只由 alloc/清零决定（0.0）。重建（src:27-30）`"31"` 扫描 + NIL→255.0，同址比较见 D1 |
| Color.initialize_copy | 0x100065a0 | 桩 | 原版完整实现（self==other 短路 0x100065aa；类型检查 0x100262b0/0x10058510/0x10058a20 失败抛错 0x10103ea4；四 double 拷贝 0x100065eb-0x10006606）。重建**未注册该方法**（src/rgss_data_classes.c 无 initialize_copy），dup/clone 落到 Object 默认行为，行为回退（D2） |
| Color.set | 0x10006610 | 一致 | 原版 0x10006610-0x10006620 直接尾调 0x100063b0（initialize）；重建 src:34-37 同样转发 color_initialize，仅多一次 0x10008f70 通知调用（原版 set 无通知，见 D6） |
| Color.to_s | 0x10006640 | 等价 | 原版四 double 经格式串（数据段加密，文本未验证）拼串返回；重建 src:39-45 `sprintf("(%f, %f, %f, %f)")`。格式文本存在偏差风险（D8） |
| Color._dump | 0x10010030 | 偏差 | 原版 xmalloc(0x20)（0x10010041），4 个 double 原样 memcpy 成 **32 字节小端 IEEE754** 字符串；重建 src:64-70 `sprintf("%f,%f,%f,%f")` 文本序列化。字节布局不兼容原版存档（D3）。共同点：均忽略 depth |
| Color._load | 0x100066c0 | 偏差 | 原版 Data_Make_Struct 清零（0x100066c3-0x100066f6），StringValue+取 len（0x10006703→0x10080860→0x10080860 返回 [ptr,len] 结构）后按 len==0x10（4×int32）/0x20（4×double）/否则全 0.0 三分支读入（0x1000671a/0x1000673c/0x1000675e）；**不调用 initialize**。重建 src:73-81 sscanf 文本解析且不区分长度。与重建 _dump 配对时自洽，但对原版存档数据不兼容（D3） |
| Color.red | 0x10010080 | 一致 | 原版 fld [+8]→0x100552c0 包装 float；重建 COLOR_GETTER 返回 rb_float_new(r)（src:47-48, 52） |
| Color.red= | 0x100100a0 | 等价 | 原版 NUM2DBL→存 [+8]→0x10008f70 通知；重建 src:50 直接赋值，无通知钩子（钩子为内部实现，行为面一致，D6）。返回 v 一致（0x1000681a `mov eax,esi`） |
| Color.green | 0x10006780 | 一致 | 同 red，fld [+0x10]（0x10006790） |
| Color.green= | 0x100067a0 | 等价 | 同 red=，存 [+0x10]（0x1000680e） |
| Color.blue | 0x10006820 | 一致 | 同 red，fld [+0x18]（0x10006830） |
| Color.blue= | 0x10006840 | 等价 | 同 red=，存 [+0x18]（0x100068ae） |
| Color.alpha | 0x100068c0 | 一致 | fld [+0x20]（0x100068d0） |
| Color.alpha= | 0x10010120 | 一致 | NUM2DBL→存 [+0x20]（0x1001018a）+0x10008f70 通知 |
| Color.== | 0x100068e0 | 等价 | 原版类型检查（0x10009dd0 失败抛 TypeError）→4× double fcomp（0x1000690c-0x10006941）→Qtrue/Qfalse；类型不符**抛异常**而非返回 false。重建 src:57-62 `rb_obj_is_kind_of` 失败返回 Qfalse。数值比较面一致 |
| Color.=== | 0x100068e0 | 等价 | 同上共享同址 |
| Color.eql? | 0x100068e0 | 等价 | 同上共享同址 |

### Tone（14 条 + 共享）

| class.method | 原版地址 | 判定 | 证据摘要 |
|---|---|---|---|
| Tone.alloc_func | 0x1000fdd0 | 偏差 | 原版 Color/Tone 共用 0x1000fdd0（Tone 注册 0x1001022d `push 0x1000fdd0`）；重建 tone_alloc src:87 独立 calloc。同 D0 |
| Tone.initialize | 0x1000fe10 | 等价 | 与 Color#initialize 同构（argc 3..4 检查 0x1000fe19/0x1000fe29；第 4 参 gray 走 NUM2DBL 0x1000ff7a-0x1000ffcd，无默认值分支）；重建 src:90-97 NIL→0.0 默认。同 D1 逻辑 |
| Tone.initialize_copy | 0x100065a0 | 桩 | 与 Color 共享同一原版实现；重建未注册（D2） |
| Tone.set | 0x10010000 | 一致 | 原版尾调 0x1000fe10（0x10010010）；重建 src:100 转发 tone_initialize |
| Tone.to_s | 0x10006640 | 等价 | 与 Color 共享原版实现；重建 src:102-108 同型 sprintf |
| Tone._dump | 0x10010030 | 偏差 | 原版 32 字节 double 二进制；重建 src:127-134 文本（D3） |
| Tone._load | 0x100066c0 | 偏差 | 原版 0x10/0x20 字节三分支；重建 sscanf 文本（D3） |
| Tone.red | 0x10010080 | 一致 | 与 Color 共享同址实现（Tone 注册表 0x10010243 起） |
| Tone.red= | 0x100100a0 | 等价 | 同 Color#red=，缺 0x10008f70 通知钩子（D6） |
| Tone.green | 0x10006780 | 一致 | 同上共享 |
| Tone.green= | 0x100067a0 | 等价 | 同上（D6） |
| Tone.blue | 0x10006820 | 一致 | 同上共享 |
| Tone.blue= | 0x10006840 | 等价 | 同上（D6） |
| Tone.gray | 0x100068c0 | 一致 | fld [+0x20] |
| Tone.gray= | 0x10010120 | 一致 | 存 [+0x20]（0x1001018a）+通知 |
| Tone.== | 0x100101a0 | 等价 | 原版类型检查+4× fcomp（0x100101cc-0x100101fe）；重建 src:120-125 kind_of 失败返 Qfalse |
| Tone.=== | 0x100101a0 | 等价 | 同上共享同址 |
| Tone.eql? | 0x100101a0 | 等价 | 同上共享同址 |

### Rect（17 条）

| class.method | 原版地址 | 判定 | 证据摘要 |
|---|---|---|---|
| Rect.alloc_func | 0x1000c450 | 偏差 | 原版 xmalloc(0x18)+清零（0x1000c450-0x1000c476）；重建 src:150 calloc 包 Data_Wrap_Struct。同 D0 |
| Rect.initialize | 0x1000c480 | 偏差 | 原版 **无 argc 检查**（无 0x10008f20 调用），TARB=4 声明下缺参传 Qnil，nil→`test al,1; je→0x100567d0(FIX2LONG)` 对 nil 会抛错而非置 0——实际缺参调用会在 FIX2LONG 抛 TypeError/NoMethodError（0x1000c484/0x1000c495）；重建 src:153-162 `"22"` 扫描 + NIL→0 默认，缺参静默置 0（D5）。4 参路径均为 FIX2LONG 存 [+8..+0x14]（0x1000c4e3-0x1000c4ec），一致 |
| Rect.initialize_copy | 0x1000c500 | 桩 | 原版类型检查（0x1000c50e-0x1000c533）+4× long 拷贝（0x1000c54b-0x1000c563）；重建未注册（D2） |
| Rect.set | 0x1000c570 | 等价 | 原版重排参数后调 0x1000c480（0x1000c58a）+0x10008f70 通知（0x1000c590）；重建 src:165-171 直接赋值。参数序一致（x,y,w,h） |
| Rect.empty | 0x1000c5a0 | 等价 | 原版 4 槽清零（0x1000c5b2-0x1000c5bc）+0x10008f70 通知；重建 src:173-178 置 0 无通知。行为面一致 |
| Rect.to_s | 0x1000c5d0 | 等价 | 原版格式串（加密段）拼 "%d, %d, %d, %d" 型文本（0x1000c5f7-0x1000c602 顺序 h,w,y,x 入栈→文本序 x,y,w,h）；重建 src:180-186 同序 sprintf（D8） |
| Rect._dump | 0x1000c640 | 偏差 | 原版 xmalloc(0x10)，4× int32 小端二进制（0x1000c651 `push 0x10`→0x1000c65a-0x1000c671 顺序 x,y,w,h 存入）；重建 src:205-212 文本 "%d,%d,%d,%d"（D3） |
| Rect._load | 0x1000c690 | 偏差 | 原版 Data_Make_Struct 清零（0x1000c692-0x1000c6ba）→StringValue→直接 memcpy 16 字节（0x1000c6cb-0x1000c6e2），**无长度/分支检查**；重建 src:214-222 sscanf 文本（D3） |
| Rect.x | 0x1000c6f0 | 一致 | 取 [+8]→`(v<<1)|1`（0x1000c706-0x1000c70b）；重建 INT2FIX（src:188-193） |
| Rect.x= | 0x1000c710 | 等价 | FIX2LONG 存 [+8]+0x10008f70 通知（0x1000c732-0x1000c751）；重建 NUM2INT 无通知（D6） |
| Rect.y | 0x1000c760 | 一致 | [+0xc]（0x1000c770） |
| Rect.y= | 0x1000c780 | 等价 | 存 [+0xc]+通知（0x1000c7a3）；重建（D6） |
| Rect.width | 0x1000c7d0 | 一致 | [+0x10]（0x1000c7e0） |
| Rect.width= | 0x1000c7f0 | 等价 | 存 [+0x10]+通知（0x1000c813）；重建（D6） |
| Rect.height | 0x1000c840 | 一致 | [+0x14]（0x1000c850） |
| Rect.height= | 0x1000c860 | 等价 | 存 [+0x14]+通知（0x1000c883）；重建（D6） |
| Rect.== | 0x1000c8b0 | 等价 | 原版类型检查（0x1000c8bd→0x10009dd0）+4× cmp（0x1000c8e2-0x1000c8fd）；重建 src:198-203 kind_of 失败返 Qfalse |
| Rect.=== | 0x1000c8b0 | 等价 | 同上共享同址 |
| Rect.eql? | 0x1000c8b0 | 等价 | 同上共享同址 |

### Table（9 条）

| class.method | 原版地址 | 判定 | 证据摘要 |
|---|---|---|---|
| Table.alloc_func | 0x1000e7f0 | 一致 | 原版 xmalloc(0x20)+清零+Data_Make_Struct（free hook=0x1000e7d0）；重建 src:227-232 calloc+自定义 free 释放 data 指针 |
| Table.initialize | 0x1000e830 | 偏差 | 原版 argc 检查 `1<=argc<=3`（0x1000e836/0x1000e848）；缺省 y/z 置 1（0x1000e879/0x1000e89d）；**x<1 / y<1 / z<1 均替换为 1**（0x1000e8c6-0x1000e8ec 的 setl/sub/and 序列即 `x<1?1:x`）；元素数=x*y*z 存 +0x18（0x1000e908-0x1000e905），xmalloc(2n)（0x1000e918-0x1000e922）后 **memset 清零**（0x1000e92d-0x1000e933 push 0）；重建 src:237-251 无 argc 上下界检查、负数钳制逻辑相同、calloc 清零等价；x 省略时重建取 1 一致。缺 1..3 界限检查（D5） |
| Table.initialize_copy | 0x1000e950 | 桩 | 原版类型检查+头 4 字段拷贝（0x1000e9a0-0x1000e9b8）+xmalloc(2n)（0x1000e9d1-0x1000e9e2）+**memcpy 复制数据**（0x1000e9ea-0x1000e9f5）；重建未注册（D2），Table.dup 数据不复制 |
| Table.resize | 0x1000ea10 | 桩 | 原版核心是 argc 检查（0x1000ea18/0x1000ea2b）+重算尺寸清零新缓冲（0x1000eb1c-0x1000eb49）后**保留旧数据**的三层嵌套拷贝（0x1000eb4c-0x1000ebf3，x/y/z 交集内 `mov dx,[ebp+edx*2]`→`mov [edi+ebx*2],dx`），结尾 xfree 旧缓冲（0x1000ebff-0x1000ec06）。重建 src:253-256 直接转 table_initialize → **数据全清零**，丢失原内容（D4） |
| Table._dump | 0x1000ec20 | 偏差 | 原版 20 字节头 [xsize,ysize,zsize,元素数,元素数]（0x1000ec41-0x1000ec5b 依次存 [+0x8],[+0xc],[+0x10],[+0x14],[+0x18]）+数据 xmemcpy（0x1000ec68-0x1000ec74），int32 小端；重建 src:299-314 头布局正确（两处元素数一致），**memcpy(&xsize,...) 直拷 int 在小端等价**；但重建头第 2/3 字段用 ysize/zsize 与原版一致，实际主要差异仅头注释"末 4 字节用途待定"（src:311）——已确认即元素数副本，行为一致。整体维持偏差评级因 len 语义待实测（原版 n 存 [edi+4] 与 [edi+8] 均为元素数，重建 n 为 x*y*z 与原版 [+0x18] 同义，实际一致）→ 若 len 计算一致可升为一致。保守判偏差（见 D9 修正说明） |
| Table._load | 0x1000eca0 | 偏差 | 原版 Data_Make_Struct 清零（0x1000eca3-0x1000ecd1）→StringValue→**直接按 +0..+0x10 读 5 个 int32**（0x1000ece4-0x1000ed03，无长度/维度检查、无钳制）→xmalloc(2n)（0x1000ed0f-0x1000ed20）→memcpy 数据（0x1000ed2b-0x1000ed33）；重建 src:316-338 增加了 ndims<3/2 归 1、尺寸<1 钳 1、len<20 短路等**原版没有的防御逻辑**（对畸形数据行为不同：原版会按头分配任意大内存/负数），且重建检查 `len>=20+n*2` 不满足时静默丢弃数据（原版直接 memcpy 可能越界）。多写的防御逻辑构成行为差（D7） |
| Table.[] | 0x1000ed50 | 等价 | 原版 argc 检查：argc 必须==xsize 槽值（0x1000ed6d `cmp ebx,eax` 即要求 argc==尺寸参数个数，越界抛 ArgumentError 0x1000ed73）；x 缺省置 0（0x1000ed9d）、y 置 0（0x1000edc2）、z 置 0（0x1000ede3）；边界 `0<=x<xsize && 0<=y<ysize && 0<=z<zsize`（0x1000ede5-0x1000ee02），越界返回 **0（Fixnum 4）**（0x1000ee26）；命中 `data[(z*ysize+y)*xsize+x]`（0x1000ee04-0x1000ee11）经 0x1001d850 包装。重建 src:258-270 同公式同越界返 0，**缺 argc==维度数检查**（1 参调用原版仅当 xsize==1 才合法，重建总是接受）。公式与越界行为一致→等价（argc 严格性差计入 D5） |
| Table.[]= | 0x1000ee30 | 等价 | 原版 argc==xsize 检查（0x1000ee50-0x1000ee5a）；边界同 []（0x1000eec5-0x1000eee6）；越界返回 4（Qnil? 实际 mov eax,0x4 即 **Qnil**，0x1000ef2c）；命中：最后参 FIX2LONG（0x1000eef0-0x1000ef0a）→`mov WORD PTR [ecx+esi*2],ax`（0x1000ef10，截断到 int16）→回读 movsx 包装返回（0x1000ef14-0x1000ef1c）。重建 src:272-293 越界返 Qnil 一致、写 short 截断一致、返回值：原版返回写入值，重建返回 Qnil（0x1000ef1b `push eax`→0x1001d850 返回 movsx 后的值）→**重建返回值偏差**（见 D5b），且缺 argc==维度数检查。综合因返回值差异判偏差（D5b） |
| Table.xsize | 0x1000ef40 | 一致 | 原版取 [+0xc]→jmp 0x1001d850（0x1000ef50-0x1000ef5b）；重建 INT2FIX(xsize)（src:295） |
| Table.ysize | 0x1000ef60 | 一致 | [+0x10]（0x1000ef70-0x1000ef7b） |
| Table.zsize | 0x1000ef80 | 一致 | [+0x14]（0x1000ef90-0x1000ef9b） |

### Font（21 条）

| class.method | 原版地址 | 判定 | 证据摘要 |
|---|---|---|---|
| Font.alloc_func | 0x100074a0 | 一致 | xmalloc(0xc)+清零+Data_Make_Struct（free hook 0x10007470：释放内部 GDI 对象+0x1003d8a0）；重建 src:347-349 calloc(0xc 等效)+font_free |
| Font.initialize | 0x10006bd0 | 偏差 | 原版 rb_obj_setup 关键字参数解析（0x10006c10 调 0x10021360，arg 槽 0x101040e4 处理 name/size/bold/italic/shadow/color 六个 kw）+重建 GDI 内部句柄（0x10006c1c xmalloc 0x370→0x10012720 初始化）+从 default_* 读取（0x1008c8e0 取 ivar）→0x10037d70 应用 kw→**0x10006d47 调 0x10006b80（Font.new→initialize_copy(default_font) 型克隆）**+0x1008bf90 写回 shadow。重建 src:352-370 忽略参数，固定从 default_* 拷贝（kw 参数全部不生效，`Font.new("MS PGothic", 24)` 与 `Font.new` 等价——**kw 不生效**）。IVAR 名为加密串。综合偏差：kw 不处理（D5c） |
| Font.initialize_copy | 0x10006df0 | 桩 | 原版类型检查（0x10006e23-0x10006e48）+xmalloc(0x370) 新内部句柄（0x10006e60-0x10006e88）+0x1008bf70 逐 ivar 取源（0x10006e99/0x10006ebd/0x10006ee3/0x10006f08/0x10006f2d）→0x10037d70 应用→0x10006f5d 调 0x10006b80 克隆内部句柄；重建未注册（D2），Font.dup 不复制内部字体句柄与颜色 |
| Font.name | 0x10006fb0 | 一致 | 0x1008bf70(ivar 0x10103e50) 返回（0x10006fb4-0x10006fba）；重建 src:372 返回存储值 |
| Font.name= | 0x100074d0 | 桩 | 原版核心：类型检查后**遍历数组每个名字调 0x10012fe0（GDI 字体存在性校验）**，存在才 0x10012860 重建内部字体句柄（0x10007519-0x1000756d 数组分支 / 0x10007575-0x100075ae 标量分支）；重建 src:373 仅 `f->name = v; return v`，**无字体存在性验证、无内部句柄重建**。渲染层的名字回退行为由 draw_text 侧补偿，但 setter 本身不完整（D10） |
| Font.size | 0x10006fd0 | 一致 | 0x1008bf70(0x10103e40)（0x10006fd4-0x10006fda） |
| Font.size= | 0x10006ff0 | 等价 | 原版 FIX2LONG→**范围检查 `6<=size<=0x60`（0x10007027-0x1000702a `push 6/push 0x60/0x10008f40`，越界抛 "bad value for %s"）**→0x100128a0 重建句柄→0x1008bf90 回写；重建 src:375 直接赋值无范围检查。行为面（size 存取）等价，缺校验（D11） |
| Font.bold | 0x10007070 | 一致 | getter 0x1008bf70(0x10103e30) |
| Font.bold= | 0x10007090 | 一致 | `v==Qnil||v==Qfalse?Qfalse:Qtrue`（0x10007094-0x100070a6 的 cmp/test 序列）→0x100128f0(bold=0x190/0x2bc)→0x1008bf90；重建 RTEST 转换（src:376-377） |
| Font.italic | 0x100070f0 | 一致 | 同 bold 结构（0x10007114-0x10007126 sete al→0x10012910） |
| Font.italic= | 0x10007110 | 一致 | 同上 |
| Font.shadow | 0x10007170 | 一致 | 同 bold 结构（0x10007194-0x100071c2→0x10012930） |
| Font.shadow= | 0x10007190 | 一致 | 同上 |
| Font.color | 0x100071f0 | 一致 | 0x1008bf70(0x10103e10)（0x100071f4-0x100071fa）；重建懒创建 Color(255,255,255,255)（src:383-391）。差异：原版 color ivar 初始化在 Font.new 时由 0x10037d70 填充（值来自 default_color 克隆），懒创建时序不同但取值一致→一致 |
| Font.color= | 0x10007210 | 偏差 | 原版 0x10009dd0 类型检查（非 Color 抛 TypeError，0x1000721e）+**深拷贝**：0x1008bf70(0x10103e10) 取旧 Color→0x1000724a 读源 4 double→0x1000724f-0x1000726a 直接 fstp 覆盖旧 Color 对象内部（`fld [esi+8]→fstp [ebx+8]` 等 4 组）；重建 src:392 仅引用赋值 `f->color = v`。行为差异：a) 非 Color 实参原版抛 TypeError、重建接受任意值；b) 原版共享/覆盖目标 Color 内部值，重建替换引用（影响 `f.color = c; c.set(...)` 后 font.color 是否联动） |
| Font.default_name | 0x10007280 | 一致 | 0x1008c8e0(0x10172010, 0x101040d4)（0x10007285-0x1000728b）；重建返回类级变量（src:395） |
| Font.default_name= | 0x100072a0 | 一致 | 0x1008c8a0 写回（0x100072a6-0x100072b1）；重建（src:396） |
| Font.default_size | 0x100072c0 | 一致 | 0x1008c8e0(0x101040c4)（0x100072c5-0x100072cb） |
| Font.default_size= | 0x100072e0 | 等价 | 原版 FIX2LONG+**范围检查 `6<=size<=0x60`**（0x100072fe-0x10007303 `push 6/push 0x60/0x10008f40`）→0x1008c8a0；重建 src:397-398 无范围检查（D11） |
| Font.default_bold | 0x10007320 | 一致 | 0x1008c8e0(0x101040b4)（0x1000732a-0x1000732b） |
| Font.default_bold= | 0x10007340 | 一致 | 0x1008c8a0（0x1000734b-0x10007351） |
| Font.default_italic | 0x10007360 | 一致 | (0x101040a0)（0x1000736b） |
| Font.default_italic= | 0x10007380 | 一致 | 0x1008c8a0（0x10007391） |
| Font.default_shadow | 0x100073a0 | 一致 | (0x1010408c)（0x100073ab） |
| Font.default_shadow= | 0x100073c0 | 一致 | 0x1008c8a0（0x100073d1） |
| Font.default_color | 0x100073e0 | 一致 | 0x1008c8e0(0x10104074)（0x100073e5-0x100073eb） |
| Font.default_color= | 0x10007400 | 等价 | 原版 0x10009dd0 类型检查（非 Color 抛 TypeError，0x1000740c）→0x1008c8a0；重建 src:405-406 无类型检查 |
| Font.exist? | 0x10007430 | 桩 | 原版 StringValue（0x100803f0）→GetGlyphOutlineW 系 GDI 查询（0x10008be0→ds:0x1016efe4+0x260 渲染上下文→0x10012fe0）→`neg eax; sbb eax,eax; and eax,0x2`（0x10007464-0x10007468）返回 Qtrue(2)/Qfalse(0)；重建 src:407 **恒返回 Qtrue**（D12） |

### 注册序列

Init_Color（0x10006950）/ Init_Tone（0x10010210）/ Init_Rect（0x1000c910 起）/ Init_Table（0x1000efa0 起）/ Init_Font（0x100075d0）注册的方法名/argc 与 TSV 一致（如 Color: initialize -1, initialize_copy 1, set -1, to_s 0, _dump 1, _load 1 单例, red 0, red= 1 … ==/===/eql? 1；Font 全部 21 项含 exist? 1）。重建注册表（src/rgss_data_classes.c:410-532）与 TSV 对照：**Color/Tone/Rect/Table/Font 均缺 initialize_copy 注册**（TSV 有、注册代码无），其余方法名与 arity 一致。

## 偏差清单（编号引用）

- **D0（alloc 层）** Color/Tone/Rect 的 alloc：原版是裸 xmalloc+memset+Data_Make_Struct（0x1000fdd0/0x1000c450），重建用 calloc+Data_Wrap_Struct。净效果（零初始化+GC free hook）一致，实现不同，属低风险偏差；Table/Font 判一致因重建结构与原版逐字节等价。
- **D1（alpha/gray 默认值）** 原版 Color#initialize/Tone#initialize 无 NIL→255/0 分支：argc==4 时第 4 参必过 NUM2DBL（0x10006519-0x10006572 / 0x1000ff7a-0x1000ffcd），省略时的值来自 alloc 清零（0.0）。重建写入 NIL→255.0（src:30）与 NIL→0.0（src:96）。净效果差异：原版 `Color.new(1,2,3).alpha == 0.0`，重建 == 255.0。**这是行为可观察的偏差**（若实测证明游戏脚本依赖 alpha=255 初值，说明原版在 Init_Color 处另有补救，但汇编内未见；按汇编为准判偏差）。
- **D2（initialize_copy 缺失）** Color/Tone/Rect/Table/Font 五类的 initialize_copy（0x100065a0/0x100065a0/0x1000c500/0x1000e950/0x10006df0）在重建中均未注册。影响：`dup`/`clone` 回退到 Object#dup（对 Data 对象复制引用而非内容），Table#dup 数据不复制、Font#clone 内部句柄不重建。原版五处均有完整拷贝逻辑。
- **D3（_dump/_load 布局）** 原版：Color/Tone 32B IEEE754 double（0x10010041 xmalloc 0x20），Rect 16B int32（0x1000c651），Table 20B int32 头+int16 数组（0x1000ec35 `lea ebx,[ebx+ebx*1+0x14]`）。重建全部用文本 sprintf/sscanf 序列化（src:64-81/127-144/205-222）。与重建自身配对自洽，但 Marshal.load 兼容原版存档（rm 数据文件内嵌 Color/Rect/Table）必然失败/错读。**高风险**：游戏数据文件中的 Table（Map 信息）用原版二进制格式。
- **D4（Table#resize 数据丢失）** 原版 resize 保留交集数据（三层循环拷贝 0x1000eb4c-0x1000ebf3）；重建转 initialize 清零（src:253-256）。
- **D5（argc/边界严格性）** 原版多处 argc 检查重建缺失：Rect#initialize（TARB=4 无上下界检查，缺参直接 FIX2LONG(Qnil) 抛 TypeError，重建却静默置 0，src:153-162）；Table#initialize/resize（1..3 检查 0x1000e836/0x1000e848）；Table#[]/[]=（argc==xsize 检查 0x1000ed6d/0x1000ee50，即 Table.new(1).t[1,2] 抛 ArgumentError 而重建接受）。
- **D5b（Table#[]= 返回值）** 原版返回写入后的值（0x1000ef14-0x1000ef1c movsx 回读→0x1001d850 包装）；重建返回 Qnil（src:292）。
- **D5c（Font.new kw 参数）** 原版 initialize 处理 name/size/bold/italic/shadow/color 关键字（0x10006c10→0x10021360 参数解析，0x10037d70 应用）；重建忽略全部参数（src:354 `(void)argc; (void)argv;`）。`Font.new("名", 大小)` 不生效。
- **D6（内部通知钩子）** 原版 setter/empty/set 尾调 0x10008f70（owner/observer 联动，Sprite/Viewport 的 color/tone 引用同对象时同步脏标记）；重建无此机制。在重建架构里渲染每帧重读 Color，净行为等价；但若未来有缓存渲染路径需补。
- **D7（Table#_load 防御逻辑多写）** 原版无任何长度/尺寸校验直接解析（0x1000ece4-0x1000ed33）；重建加了 ndims<3→zsize=1、尺寸<1 钳 1、len<20 短路、len 不足丢数据（src:328-336）。对畸形数据行为不同；对正常数据等价。
- **D8（to_s 格式文本）** 原版格式串在加密数据段（0x10103ff0/0x10104864 解密后未验证文本）；重建用 "(%f, %f, %f, %f)"/"(%d, %d, %d, %d)"。格式细节（小数位、空格）可能不同，待运行时对照。
- **D9（Table#_dump 头第 5 字段）** 已在汇编确认：原版头 = [xsize, ysize, zsize, 元素数, 元素数]（0x1000ec41-0x1000ec5b 两处 mov DWORD [edi+8/0xc] 分别存 [+0x14],[+0x18]）。重建 src:311 注释"末 4 字节用途待定"应更正为"元素数副本"；实现本身已正确。
- **D10（Font#name= 不完整）** 原版 setter 内嵌 GDI 字体存在性验证（0x10012fe0）+内部字体对象重建（0x10012860），支持数组名逐个回退（0x10007519-0x1000756d）；重建仅赋值 Ruby ivar（src:373）。
- **D11（Font size 范围检查缺失）** 原版 Font#size= 与 default_size= 都有 `6<=v<=0x60` 检查（0x10007027-0x1000702a / 0x100072fe-0x10007303，越界抛 "bad value for %s"）；重建无检查（src:375/397-398）。
- **D12（Font.exist? 恒真）** 原版经 GDI 枚举验证字体存在（0x1000745e→0x10012fe0→neg/sbb 返回 2/0）；重建恒 Qtrue（src:407）。

## 汇总

注册序列核对：Init_Color（0x10006950）/ Init_Tone（0x10010210）/ Init_Rect（0x1000c910 起）/ Init_Table（0x1000efa0 起）/ Init_Font（0x100075d0）注册的方法名/argc 与 TSV 一致。重建注册表（src/rgss_data_classes.c:410-532）与 TSV 对照：**五类均缺 initialize_copy 注册**（TSV 有、注册代码无），其余方法名与 arity 一致。

按 TSV 全部 94 行（==/===/eql? 各计 1 行，alloc_func 计 1 行）逐行点数：

| 判定 | 数量 |
|---|---|
| 一致 | 40 |
| 等价 | 29 |
| 偏差 | 17 |
| 桩 | 8 |
| 未映射 / 无法验证 | 0 |
| 合计 | 94 |

分类：Color 18 行（一致 6 / 等价 7 / 偏差 4 / 桩 1）；Tone 18 行（6 / 8 / 3 / 1）；Rect 19 行（4 / 10 / 4 / 1）；Table 11 行（4 / 1 / 4 / 2）；Font 28 行（20 / 3 / 2 / 3）。

偏差/桩条目（按类）：
- Color：alloc_func(D0)、initialize(D1)、initialize_copy(D2 桩)、_dump(D3)、_load(D3)、red=/green=/blue=(D6)、==/===/eql?(异常 vs false)
- Tone：alloc_func(D0)、initialize(D1)、initialize_copy(D2 桩)、_dump(D3)、_load(D3)、==/===/eql?
- Rect：alloc_func(D0)、initialize(D5)、initialize_copy(D2 桩)、_dump(D3)、_load(D3)、==/===/eql?
- Table：initialize(D5)、initialize_copy(D2 桩)、resize(D4 桩)、_dump(D3/D9)、_load(D3/D7)、[](D5)、[]=(D5/D5b)
- Font：initialize(D5c)、initialize_copy(D2 桩)、name=(D10 桩)、size=(D11)、default_size=(D11)、color=(类型检查+深拷贝)、default_color=(类型检查)、exist?(D12 桩)

最高风险三项：**D3（_dump/_load 二进制格式）→ 原版存档不兼容；D4（Table#resize 清数据）→ 运行时数据丢失；D2（initialize_copy 全缺）→ dup/clone 语义错误**。
