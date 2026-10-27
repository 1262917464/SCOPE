# A1 — 数据类（Color / Tone / Rect / Table / Font）白盒一致性审计
> 注：本文写于 2026-10-27 仓库重构前；文中 `backup/` `unpacked/` `build/` `test/` `third_party/` 等路径现位于 `local/` 下，`src/rgss_*.c` 现为 `src/runtimes/rgss2/*.c`，`tools/` 已按流水线分桶（unpack/pe/analysis/probe/verify）。


- 审计日期：2026-10-27（只读审计，未改动任何 src/ 文件）
- 重建源码：`src/rgss_data_classes.c`
- 原版参考：`docs/analysis/rgss2/rgss_ruby_api_map.tsv` + `build/full_disasm.txt`（objdump，基址 0x10000000）
- 方法：按 TSV 的 fn_va 逐函数解码汇编（数据类函数均不在 Hex-Rays 缓存内，全部读汇编）；关键常量/字符串用 `backup/RGSS202E.dll` 原始字节核对（数据段已加壳加密，字符串直接读取为乱码，故文本类常量标注"未验证"）。
- 判定枚举：一致 / 等价（实现不同但行为同）/ 偏差 / 桩 / 未映射 / 无法验证

## 公共辅助 thunk 语义（读汇编所得）

| 地址 | 语义 | 证据 |
|---|---|---|
| 0x10008f20 | argc 上下界检查（失败抛 ArgumentError） | Color#initialize 0x100063b3/0x100063c9 |
| 0x10008f40 | 数值范围检查 `(v<lo\|\|v>hi)` 失败抛 "bad value for %s" | 缓存伪代码 0x10008f40.c；Font#size= 0x10007027 |
| 0x10008f70 | 内部"修改通知"（owner/observer ivar 同步钩子，非 Ruby 公开 API） | 缓存伪代码 0x10008f70.c |
| 0x10009dd0 | 类型转换检查，失败抛 TypeError "can't convert %s into %s" | 缓存伪代码 0x10009dd0.c |
| 0x1005a2a0 | NUM2DBL | Color#initialize 全程经此转 double |
| 0x100567d0 | FIX2LONG（非 Fixnum 走此路径） | Rect#initialize 0x1000c495 |
| 0x1001d850 | FIXNUM 标记化（int→VALUE） | Table#[] 0x1000ee15 |
| 0x100552c0 | double→rb_float_new | Color#red 0x10006796 |
| 0x10026860 | Data_Get_Struct（0x22=16 安全等级参数） | 所有实例方法入口 |
| 0x1003f5a0 / 0x1003f790 | ruby_xmalloc / Data_Make_Struct alloc | 各 alloc_func |
| 0x100dc03f / 0x100dc6e0 / 0x100de190 / 0x100dc107 | ruby_xmalloc / xmemset / xmemcpy / xfree | Table 系列 |
| 0x1008bf70 / 0x1008bf90 | ivar_get / ivar_set | Font 全部属性 |
| 0x1008c8e0 / 0x1008c8a0 | 类级 ivar get / set（default_*） | Font 单例方法 |
| 0x100262b0+0x10058510+0x10058a20 | rb_obj_is_kind_of 检查（失败 0x10025f80 抛 TypeError） | initialize_copy 系列 |
| 0x10037d70 | 内部"应用属性"例程 | Font initialize/initialize_copy |
| 0x10012860 / 0x100128f0 / 0x10012910 / 0x10012930 | 内部字体对象重建（name/size/bold/italic/shadow） | Font setters |
| 0x10012fe0 | GDI 字体存在性查询（渲染上下文 ds:0x1016efe4+0x260） | Font#name= / exist? |

关键 Ruby 值编码（由 Font#bold= 0x10007094-0x100070a6 与 Init_Font 0x100079b0-0x10007a00 确认）：Qfalse=0x0、Qtrue=0x2、**Qnil=0x4**（`cmp eax,0x4; je` 判 nil 分支）、Fixnum n = 2n+1（0x29→20，0x3ff→255）。

## 原版数据布局

- Color/Tone：Data 指针指向 0x28 字节块，4 个 double：`+8 r, +0x10 g, +0x18 b, +0x20 a(Color)/gray(Tone)`（initialize_copy 0x100065eb 起四连 fld/fstp 证实）。
- Rect：0x18 字节块，4 个 long：`+8 x, +0xc y, +0x10 w, +0x14 h`（initialize_copy 0x1000c54b）。
- Table：0x20 字节块：`+8 创建时argc, +0xc xsize, +0x10 ysize, +0x14 zsize, +0x18 元素数, +0x1c short*`（0x1000e8fa-0x1000e933：ebp 存 +8，三个尺寸存 +0xc/+0x10/+0x14，乘积存 +0x18）。
- Font：0xc 字节块，单一内部句柄槽 +8（xmalloc 0x370 由 0x10012720 初始化）；全部属性存 Ruby ivar。
- Color/Tone 共享 initialize/_dump/_load/getter/setter/== 的实现（同址），差异仅在 4th 字段语义（a vs gray）。

## 逐条判定

### Color（18 行）

| class.method | 原版地址 | 判定 | 证据摘要 |
|---|---|---|---|
| Color.alloc_func | 0x1000fdd0 | 一致 | xmalloc(0x28)+清零+Data_Make_Struct（0x1000fdd0-0x1000fe02）；Init_Color 0x10006969 `push 0x1000fdd0`。重建 calloc+Data_Wrap_Struct 净效果相同 |
| Color.initialize | 0x100063b0 | 偏差 | ①argc 检查 3..4（0x100063b3/0x100063c9），重建 rb_scan_args"31"同效；②r/g/b 逐字段三分支钳制：NUM2DBL 后 `fcomp [0x10103fe8](=C_up)` 不小于则保持（0x100063e5-0x100063f3）→与 `[0x10103fe0](=C_low)` 比较取上界（0x100063f5-0x1000640f）→else 保持原值（0x10006411-0x10006439）；③第 4 参 a 无 255 默认分支（argc==4 时仍走 NUM2DBL 0x10006519-0x10006572），省略时由 alloc 清零=0.0。重建（src:27-30）**无任何钳制**且 a 的 NIL 默认为 255.0，与原版 a=0.0 相反。双常量值因数据段加密未能读出（见 D13），但"有钳制"这一行为差确定 |
| Color.initialize_copy | 0x100065a0 | 桩 | 原版 self==other 短路（0x100065aa）+类型检查抛错（0x100065ae-0x100065d3）+四 double 拷贝（0x100065eb-0x10006606）；重建未注册该方法（src 无），dup/clone 回退 Object#dup |
| Color.set | 0x10006610 | 一致 | 原版尾调 initialize 0x100063b0（0x10006620）；重建 src:34-37 转发 color_initialize，无钳制（差异归 D1，此处结构一致） |
| Color.to_s | 0x10006640 | 无法验证 | 原版取四 double 经加密格式串 0x10103ff0 格式化（0x1000668b `push 0x10103ff0`→0x100dcec1）；字符串解密后文本未验证，重建 "(%f, %f, %f, %f)"（src:39-45）无法比对文本 |
| Color._dump | 0x10010030 | 偏差 | 原版 xmalloc(0x20)（0x10010041）把 4 个 double 顺序存入（0x10010048-0x10010061：fld [+8]/[+0x10]/[+0x18]/[+0x20]→fstp），32 字节 IEEE754 小端；忽略 depth。重建 src:64-70 `sprintf("%f,%f,%f,%f")` 文本（D3） |
| Color._load | 0x100066c0 | 偏差 | 原版 Data_Make_Struct 清零→StringValue→按 str len 三分支：0x10→4×fild int32（0x1000671a-0x10006735）；0x20→4×fld double（0x10006741-0x10006758）；否则全 0.0（0x1000675e-0x1000676c）。重建 src:73-81 sscanf 文本且不区分长度（D3） |
| Color.red | 0x10010080 | 一致 | fld [+8]→0x100552c0（0x10010090-0x10010096）；重建 rb_float_new（src:47-48,52） |
| Color.red= | 0x100100a0 | 偏差 | 原版带同 initialize 的三分支钳制（0x100100b7-0x1001010a 与 initialize 逐指令同型）后存 [+8]（0x1001010e）+0x10008f70 通知；重建 src:50 直接赋值无钳制（D1） |
| Color.green | 0x10006780 | 一致 | fld [+0x10]→0x100552c0（0x10006790-0x10006796） |
| Color.green= | 0x100067a0 | 偏差 | 同 red= 的钳制结构（0x100067b7-0x1000680a）存 [+0x10]；重建无钳制（D1） |
| Color.blue | 0x10006820 | 一致 | fld [+0x18]（0x10006830） |
| Color.blue= | 0x10006840 | 偏差 | 同上钳制（0x10006857-0x100068aa）存 [+0x18]；重建无钳制（D1） |
| Color.alpha | 0x100068c0 | 一致 | fld [+0x20]（0x100068d0） |
| Color.alpha= | 0x10010120 | 偏差 | 同钳制，仅下界分支为 fldz（0.0，0x10010152 `d9 ee`，alpha 下界与 rgb 不同）后存 [+0x20]（0x1001018a）；重建无钳制（D1） |
| Color.== | 0x100068e0 | 等价 | 类型检查（0x100068ed→0x10009dd0 失败抛 TypeError）→4× fcomp 对四字段（0x1000690c-0x10006941）；重建 src:57-62 kind_of 失败返 Qfalse 不抛异常，数值面一致 |
| Color.=== | 0x100068e0 | 等价 | 同上共享同址 |
| Color.eql? | 0x100068e0 | 等价 | 同上共享同址 |

### Tone（18 行）

| class.method | 原版地址 | 判定 | 证据摘要 |
|---|---|---|---|
| Tone.alloc_func | 0x1000fdd0 | 一致 | 与 Color 共用（Tone 注册 0x1001022d `push 0x1000fdd0`） |
| Tone.initialize | 0x1000fe10 | 偏差 | 与 Color#initialize 同构：argc 3..4（0x1000fe19/0x1000fe29）+r/g/b 钳制（0x1000fe45-0x1000ff6a）+gray 走 NUM2DBL 无默认分支（0x1000ff7a-0x1000ffcd，fldz 仅作下界比较基准）；重建 src:90-97 无钳制、NIL gray→0.0（恰好与原版省略值一致，但钳制缺失，D1） |
| Tone.initialize_copy | 0x100065a0 | 桩 | 与 Color 共享；重建未注册（D2） |
| Tone.set | 0x10010000 | 一致 | 原版尾调 0x1000fe10（0x10010010）+0x10008f70 通知；重建 src:100 转发 tone_initialize |
| Tone.to_s | 0x10006640 | 无法验证 | 与 Color 共享（加密格式串）；重建 src:102-108 同型 sprintf |
| Tone._dump | 0x10010030 | 偏差 | 32 字节 double 二进制 vs 重建文本（D3） |
| Tone._load | 0x100066c0 | 偏差 | 0x10/0x20 字节三分支 vs sscanf 文本（D3） |
| Tone.red | 0x10010080 | 一致 | 共享 |
| Tone.red= | 0x100100a0 | 偏差 | 带钳制；重建无（D1） |
| Tone.green | 0x10006780 | 一致 | 共享 |
| Tone.green= | 0x100067a0 | 偏差 | 带钳制；重建无（D1） |
| Tone.blue | 0x10006820 | 一致 | 共享 |
| Tone.blue= | 0x10006840 | 偏差 | 带钳制；重建无（D1） |
| Tone.gray | 0x100068c0 | 一致 | fld [+0x20] |
| Tone.gray= | 0x10010120 | 偏差 | 带钳制（下界 fldz 0x10010152）；重建无（D1） |
| Tone.== | 0x100101a0 | 等价 | 类型检查（0x100101ad→0x10009dd0）+4× fcomp（0x100101cc-0x100101fe）；重建 kind_of 失败返 Qfalse |
| Tone.=== | 0x100101a0 | 等价 | 同上共享同址 |
| Tone.eql? | 0x100101a0 | 等价 | 同上共享同址 |

### Rect（19 行）

| class.method | 原版地址 | 判定 | 证据摘要 |
|---|---|---|---|
| Rect.alloc_func | 0x1000c450 | 一致 | xmalloc(0x18)+清零+Data_Make_Struct（0x1000c450-0x1000c476）；重建等价 |
| Rect.initialize | 0x1000c480 | 偏差 | 原版**无 argc 检查**（无 0x10008f20），TARB=4 下缺参实参为 Qnil，nil→`test al,1; je→0x100567d0(FIX2LONG)` 对 nil 抛错（0x1000c484/0x1000c495）——缺参不静默；4 参均为 FIX2LONG 存 [+8..+0x14]（0x1000c4e3-0x1000c4ec），**无钳制**。重建 src:153-162 `"22"` 扫描 + NIL→0 静默默认，缺参行为不同（D5） |
| Rect.initialize_copy | 0x1000c500 | 桩 | 类型检查（0x1000c50e-0x1000c533）+4× long 拷贝（0x1000c54b-0x1000c563）；重建未注册（D2） |
| Rect.set | 0x1000c570 | 一致 | 原版重排参数调 0x1000c480（0x1000c58a）+0x10008f70 通知；重建 src:165-171 同序赋值 |
| Rect.empty | 0x1000c5a0 | 一致 | 4 槽清零（0x1000c5b2-0x1000c5bc）+通知；重建 src:173-178 置 0 |
| Rect.to_s | 0x1000c5d0 | 无法验证 | 加密格式串 0x10104864（0x1000c60b）；文本未验证 |
| Rect._dump | 0x1000c640 | 偏差 | 原版 xmalloc(0x10) 存 4× int32 小端（0x1000c651 `push 0x10`→0x1000c65a-0x1000c671 顺序 x,y,w,h）；重建 src:205-212 文本 "%d,%d,%d,%d"（D3） |
| Rect._load | 0x1000c690 | 偏差 | 原版 Data_Make_Struct 清零→StringValue→**直接 memcpy 16 字节**（0x1000c6cb-0x1000c6e2），无长度检查；重建 src:214-222 sscanf 文本（D3） |
| Rect.x | 0x1000c6f0 | 一致 | 取 [+8]→`(v<<1)\|1`（0x1000c706-0x1000c70b） |
| Rect.x= | 0x1000c710 | 一致 | FIX2LONG 存 [+8]（0x1000c729-0x1000c75e）+通知；重建 NUM2INT（src:190-191） |
| Rect.y | 0x1000c760 | 一致 | [+0xc]（0x1000c770） |
| Rect.y= | 0x1000c780 | 一致 | 存 [+0xc]（0x1000c7a3） |
| Rect.width | 0x1000c7d0 | 一致 | [+0x10]（0x1000c7e0） |
| Rect.width= | 0x1000c7f0 | 一致 | 存 [+0x10]（0x1000c813） |
| Rect.height | 0x1000c840 | 一致 | [+0x14]（0x1000c850） |
| Rect.height= | 0x1000c860 | 一致 | 存 [+0x14]（0x1000c883） |
| Rect.== | 0x1000c8b0 | 等价 | 类型检查（0x1000c8bd→0x10009dd0）+4× cmp（0x1000c8e2-0x1000c8fd）；重建 kind_of 失败返 Qfalse |
| Rect.=== / eql? | 0x1000c8b0 | 等价 | 同上共享同址（===、eql? 各 1 行，均共享） |

### Table（11 行）

| class.method | 原版地址 | 判定 | 证据摘要 |
|---|---|---|---|
| Table.alloc_func | 0x1000e7f0 | 一致 | xmalloc(0x20)+清零+Data_Make_Struct（free hook 0x1000e7d0）；重建同构（src:227-232） |
| Table.initialize | 0x1000e830 | 偏差 | 原版：argc 检查 1..3（0x1000e836/0x1000e848）；缺省 y/z=1（0x1000e879/0x1000e89d）；三尺寸 `<1?1:v` 钳制（0x1000e8c6-0x1000e8ec setl/sub/and 序列）；**argc 值本身存 [+8]**（0x1000e8fa `mov [esi+8],ebp`）；元素数=x*y*z（0x1000e8f4-0x1000e905）；xmalloc(2n)（0x1000e918-0x1000e922）+memset 清零（0x1000e92d-0x1000e933）。重建 src:237-251：缺 1..3 界限检查（D5）；**未存储 argc**（TableVal 无此字段，导致 _dump 头错，见 _dump 行）；钳制逻辑一致；calloc 等价 memset |
| Table.initialize_copy | 0x1000e950 | 桩 | 类型检查+头 4 字段拷贝（0x1000e9a0-0x1000e9b8，含 [+8] argc）+xmalloc(2n)（0x1000e9d1-0x1000e9e2）+memcpy 数据（0x1000e9ea-0x1000e9f5）；重建未注册（D2），Table.dup 数据不复制 |
| Table.resize | 0x1000ea10 | 桩 | 原版 argc 检查（0x1000ea18/0x1000ea2b）+钳制+清零新缓冲（0x1000eb1c-0x1000eb49）后**保留旧数据交集**的三层拷贝（0x1000eb4c-0x1000ebf3：`mov dx,[ebp+edx*2]`→`mov [edi+ebx*2],dx`），结尾 xfree 旧缓冲（0x1000ebff-0x1000ec06）；重建 src:253-256 直接转 table_initialize→**数据全清零**（D4） |
| Table._dump | 0x1000ec20 | 偏差 | 原版 20 字节头=[**argc(+8), xsize, ysize, zsize, 元素数**]（0x1000ec41-0x1000ec5b 依次存 [+8],[+0xc],[+0x10],[+0x14],[+0x18]）+数据 memcpy（0x1000ec68-0x1000ec74），int32 小端；重建 src:299-314 头=[xsize,ysize,zsize,n,n]，**首字段与原版错位**（原版首位是创建维度数，重建是 xsize），数据布局一致。与原版 _load 配对时：原版 _load 首字段读入 [+8] 不用于尺寸，尺寸取 +4..+0xc——即**原版 dump 头是 [argc,x,y,z,n]，重建是 [x,y,z,n,n]**，字节级不兼容（D3b） |
| Table._load | 0x1000eca0 | 偏差 | 原版 Data_Make_Struct 清零→StringValue→**直接读 +0..+0x10 五个 int32 到 [+8],[+0xc],[+0x10],[+0x14],[+0x18]**（0x1000ece4-0x1000ed03，无长度检查、无钳制、无维度归一）→xmalloc(2n)（0x1000ed0f-0x1000ed20）→memcpy（0x1000ed2b-0x1000ed33）。重建 src:316-338：把首 int32 当 ndims、后三个当 x/y/z（**相对原版整体后移一字段**），且加了 ndims<3→zsize=1、尺寸<1 钳 1、len<20 短路等**原版没有的防御**（D3b/D7）。对正常原版数据必错位 |
| Table.[] | 0x1000ed50 | 偏差 | 原版：argc 必须==[+8]（0x1000ed6d `cmp ebx,eax`+0x1000ed73 抛错）；x/y/z 缺省=0（0x1000ed9d/0x1000edc2/0x1000ede3）；越界返回 **0x4（Qnil）**（0x1000ee26 `mov eax,0x4`）；命中 `(z*ysize+y)*xsize+x`（0x1000ee04-0x1000ee11）→movsx→0x1001d850。重建 src:258-270：缺 argc==维度检查（D5）；越界返回 INT2FIX(0)（src:267）**与原版 Qnil 不同**；公式一致 |
| Table.[]= | 0x1000ee30 | 偏差 | 原版：argc==[+8] 检查（0x1000ee50-0x1000ee5a）；同 [] 边界，越界返回 Qnil（0x1000ef2c `mov eax,0x4`）；命中：末参 FIX2LONG（0x1000eef0-0x1000ef0a）→`mov WORD PTR [ecx+esi*2],ax` 截断 int16（0x1000ef10）→**回读 movsx 包装返回写入值**（0x1000ef14-0x1000ef1c）。重建 src:272-293：缺 argc 检查（D5）；截断与越界 Qnil 一致；**返回 Qnil 而非写入值**（src:292，D5b）；参数分派（3 参=x / 4 参=x,y,val? 见 src:279-287）依赖 vval NIL 判断，与原版 argc==[+8] 语义不同 |
| Table.xsize | 0x1000ef40 | 一致 | 取 [+0xc]→jmp 0x1001d850（0x1000ef50-0x1000ef5b）；重建 INT2FIX（src:295） |
| Table.ysize | 0x1000ef60 | 一致 | [+0x10]（0x1000ef70-0x1000ef7b） |
| Table.zsize | 0x1000ef80 | 一致 | [+0x14]（0x1000ef90-0x1000ef9b） |

### Font（28 行）

| class.method | 原版地址 | 判定 | 证据摘要 |
|---|---|---|---|
| Font.alloc_func | 0x100074a0 | 一致 | xmalloc(0xc)+清零+Data_Make_Struct（free hook 0x10007470）；重建 src:347-349 同构 |
| Font.initialize | 0x10006bd0 | 偏差 | 原版：0x10021360 参数解析（0x10006c10，含 kw 槽 0x101040e4）+xmalloc(0x370) 内部句柄（0x10006c1c-0x10006c3b）+从 default_* 逐项取值（0x1008c8e0 0x101040d4/0xc4/0xb4/0xa0/0x8c）+0x10037d70 应用（0x10006cc6-0x10006d36）+调 0x10006b80 克隆 default_color 型对象（0x10006d47）+0x1008bf90 回写（0x10006d5b-0x10006d81）+argv 逐个覆盖（0x10006d8e-0x10006dd1 argc>=1/2 分支）。重建 src:352-370 忽略全部参数（`(void)argc`），仅从 default_* 拷贝——`Font.new(name,size)` 参数不生效（D5c）；缺内部 GDI 句柄创建（由 draw_text 侧补偿） |
| Font.initialize_copy | 0x10006df0 | 桩 | 类型检查+新 0x370 句柄（0x10006e60-0x10006e88）+逐 ivar 复制（0x1008bf70→0x10037d70 五组 0x10006e99-0x10006f47）+0x10006b80 克隆+回写；重建未注册（D2） |
| Font.name | 0x10006fb0 | 无法验证 | 0x1008bf70(ivar 0x10103e50)（0x10006fb4-0x10006fba）；ivar 名与初值为加密串。重建 src:372 返回存储值，机制一致、初值文本未验证 |
| Font.name= | 0x100074d0 | 桩 | 原版核心：非数组标量先过 0x10012fe0 GDI 存在性校验，存在才 0x10012860 重建内部句柄（0x10007575-0x100075ae）；**数组分支逐元素校验**（0x10007519-0x1000756d `cmp esi,[ebx+8]` 循环）；最后 0x1008bf90 写 ivar。重建 src:373 仅 `f->name=v; return v`，无校验、无数组支持、无句柄重建（D10） |
| Font.size | 0x10006fd0 | 无法验证 | 0x1008bf70(0x10103e40)；重建 src:374 一致机制 |
| Font.size= | 0x10006ff0 | 偏差 | 原版 FIX2LONG→**范围检查 `6<=v<=0x60`**（0x10007027-0x1000702a push 6/0x60→0x10008f40，越界抛 "bad value for %s"）→0x100128a0 重建句柄→0x1008bf90 回写；重建 src:375 直接赋值无范围检查（D11） |
| Font.bold | 0x10007070 | 无法验证 | 0x1008bf70(0x10103e30)；机制一致，初值文本未验证 |
| Font.bold= | 0x10007090 | 一致 | `v==Qnil\|\|v==Qfalse?Qfalse:Qtrue`（0x10007094-0x100070a6 cmp 0x4/test 0x1）→0x100128f0(0x190/0x2bc)→回写；重建 RTEST（src:377）同语义 |
| Font.italic | 0x100070f0 | 无法验证 | 0x1008bf70(0x10103e20) |
| Font.italic= | 0x10007110 | 一致 | 同 bold= 结构（0x10007114-0x10007126 sete→0x10012910）；重建（src:379） |
| Font.shadow | 0x10007170 | 无法验证 | 0x1008bf70(0x101040e8) |
| Font.shadow= | 0x10007190 | 一致 | 同 bold= 结构（0x10007194-0x100071c2→0x10012930）；重建（src:381） |
| Font.color | 0x100071f0 | 无法验证 | 0x1008bf70(0x10103e10)；重建懒创建 Color(255,255,255,255)（src:383-391），原版初值来自 initialize 链（文本未验证） |
| Font.color= | 0x10007210 | 偏差 | 原版 0x10009dd0 类型检查（0x1000721e，非 Color 抛 TypeError）+**深拷贝**：0x1008bf70 取旧 Color→读源 4 double→直接 fstp 覆盖旧对象内部（0x1000724f-0x1000726a `fld [esi+8]→fstp [ebx+8]` 等四组）；重建 src:392 仅引用赋值。差异：非 Color 实参原版抛异常；`font.color=c` 后修改 c 原版不联动（值拷贝）、重建联动（引用） |
| Font.default_name | 0x10007280 | 无法验证 | 0x1008c8e0(0x101040d4)（0x10007285-0x1000728b）；初值文本未验证。注：Init_Font 0x10007983 调 0x10019e30（arg=3）构造初值，结合 name= 的数组遍历分支，原版默认名应为**字符串数组**；重建为单一 "MS Gothic"（src:510），待 D14 实测 |
| Font.default_name= | 0x100072a0 | 一致 | 0x1008c8a0 写回（0x100072a6-0x100072b1）；重建（src:396） |
| Font.default_size | 0x100072c0 | 无法验证 | 0x1008c8e0(0x101040c4)；初值 0x29→20（0x100079a2），重建 INT2FIX(20)（src:511）一致 |
| Font.default_size= | 0x100072e0 | 偏差 | 原版 FIX2LONG+范围检查 `6<=v<=0x60`（0x100072fe-0x10007303 push 6/0x60→0x10008f40）；重建 src:397-398 无检查（D11） |
| Font.default_bold | 0x10007320 | 一致 | 0x1008c8e0(0x101040b4)（0x1000732a）；初值 0x0=Qfalse（0x100079b4） |
| Font.default_bold= | 0x10007340 | 一致 | 0x1008c8a0（0x1000734b-0x10007351） |
| Font.default_italic | 0x10007360 | 一致 | (0x101040a0)（0x1000736b）；初值 Qfalse（0x100079cf） |
| Font.default_italic= | 0x10007380 | 一致 | 0x1008c8a0（0x10007391） |
| Font.default_shadow | 0x100073a0 | 一致 | (0x1010408c)（0x100073ab）；初值 0x2=Qtrue（0x100079e5） |
| Font.default_shadow= | 0x100073c0 | 一致 | 0x1008c8a0（0x100073d1） |
| Font.default_color | 0x100073e0 | 一致 | 0x1008c8e0(0x10104074)（0x100073e5）；初值=0x10006b20 构造的 Color(255,255,255,255)（0x100079ea-0x100079fe 四次 push 0xff）；重建 Qnil（src:518），由 font_color 懒创建补偿为 255 白，取值一致 |
| Font.default_color= | 0x10007400 | 偏差 | 原版 0x10009dd0 类型检查（0x1000740c，非 Color 抛 TypeError）→0x1008c8a0；重建 src:405-406 无类型检查 |
| Font.exist? | 0x10007430 | 桩 | 原版 StringValue（0x100803f0）→GDI 查询（0x10008be0→ds:0x1016efe4+0x260→0x10012fe0）→`neg eax; sbb eax,eax; and eax,0x2`（0x10007464-0x10007468）返回 Qtrue(2)/Qfalse(0)；重建 src:407 恒返回 Qtrue（D12） |

## 偏差清单（编号引用）

- **D1（钳制缺失）** 原版 Color/Tone 的 initialize 与全部 setter（red=/green=/blue=/alpha=/gray=）带双常量三分支钳制：值与 `[0x10103fe8]`（上界）比较→与 `[0x10103fe0]`（下界）比较取上界→否则保持。alpha=/gray= 的下界分支为 fldz（0.0）。双常量为加密数据（0x10103fe0/0x10103fe8 解密值未读出，即 D13 未验证项），但机制确定存在。重建完全无钳制（src:27-30, 50, 96, 113）。对超界输入（如 `Color.new(999,0,0)`）行为不同。
- **D2（initialize_copy 全缺）** 五类 initialize_copy（0x100065a0×2 / 0x1000c500 / 0x1000e950 / 0x10006df0）重建均未注册（src 无对应函数与 rb_define_method 行）。dup/clone 回退 Object#dup：Table 数据不复制、Font 句柄/颜色不复制、Color/Tone/Rect 内容不复制。原版五处均有完整拷贝逻辑。
- **D3（_dump/_load 二进制格式）** 原版：Color/Tone 32B IEEE754 double（0x10010041），Rect 16B int32（0x1000c651），Table 20B 头+int16 数组（0x1000ec35）。重建全用文本 sprintf/sscanf（src:64-81/127-144/205-222）。Marshal 兼容原版存档必然失败。
- **D3b（Table dump/load 头错位）** 原版头=[argc, xsize, ysize, zsize, 元素数]（0x1000ec41-0x1000ec5b 五连存 [+8],[+0xc],[+0x10],[+0x14],[+0x18]；_load 对称读入 0x1000ece4-0x1000ed03）。重建头=[xsize,ysize,zsize,n,n]（src:306-311），_load 把首字段当 ndims（src:323-324）。重建 TableVal 缺 argc 字段（src:225）。对原版数据字节级错位。
- **D4（Table#resize 数据丢失）** 原版保留交集数据（0x1000eb4c-0x1000ebf3）+xfree 旧缓冲；重建转 initialize 清零（src:253-256）。
- **D5（argc/缺参严格性）** 原版：Rect#initialize 无 argc 检查、缺参经 FIX2LONG(Qnil) 抛错（0x1000c484），重建静默置 0（src:153-162）；Table#initialize/resize 有 1..3 检查（0x1000e836/0x1000ea18），重建无；Table#[]/[]= 有 argc==维度 检查（0x1000ed6d/0x1000ee50），重建无（src:258-293）。
- **D5b（Table#[]= 返回值）** 原版返回写入值（0x1000ef14-0x1000ef1c movsx 回读→0x1001d850）；重建返回 Qnil（src:292）。
- **D5c（Font.new kw/argv 不生效）** 原版 initialize 解析参数（0x10006c10→0x10021360）并逐个覆盖（0x10006d8e-0x10006dd1）；重建忽略全部参数（src:354-355）。
- **D6（通知钩子）** 原版 setter/empty/set 尾调 0x10008f70；重建无。重建渲染每帧重读 Color 时净行为等价，缓存渲染路径需注意。
- **D7（Table#_load 防御逻辑多写）** 原版无任何校验直接解析（0x1000ece4-0x1000ed33）；重建加 ndims 归一、钳制、len 检查（src:328-336）。多写的逻辑对畸形数据行为不同，对正常数据与 D3b 错位叠加。
- **D8（to_s 格式文本未验证）** 原版格式串加密（0x10103ff0/0x10104864），文本未读出；待运行时对照。判"无法验证"而非偏差。
- **D9（Table#_dump 末 4 字节）** 已确认=元素数（[+0x18]），重建注释"待定"（src:311）应更正；但重建首字段错位（D3b）使该行仍判偏差。
- **D10（Font#name= 不完整）** 原版带 GDI 存在性校验（0x10012fe0）+句柄重建（0x10012860）+**数组名逐元素回退**（0x10007519-0x1000756d）；重建仅 ivar 赋值（src:373）。
- **D11（Font size 范围检查缺失）** 原版 Font#size= 与 default_size= 均有 `6<=v<=0x60` 检查（0x10007027-0x1000702a / 0x100072fe-0x10007303）；重建无（src:375, 397-398）。
- **D12（Font.exist? 恒真）** 原版 GDI 验证返回 Qtrue/Qfalse（0x10007464-0x10007468）；重建恒 Qtrue（src:407）。
- **D13（未验证：钳制常量与格式文本）** 0x10103fe0/0x10103fe8（Color/Tone 边界）、0x10103ff0/0x10104864（to_s 格式）、Font ivar 名与默认名文本——数据段加密（backup/RGSS202E.dll 原始字节为乱码），需运行时 dump 后二次核对。
- **D14（未验证：Font.default_name 初值形态）** Init_Font 0x10007983 以 arg=3 调 0x10019e30 构造初值，且 name= 有数组分支——原版默认名大概率为字符串数组（如 ["MS Gothic","MS PGothic",...]）；重建为单一 String（src:510）。待解密/运行时确认后或升级为偏差。

## 汇总

注册序列核对：Init_Color（0x10006950）/ Init_Tone（0x10010210）/ Init_Rect（0x1000c910 起）/ Init_Table（0x1000efa0 起）/ Init_Font（0x100075d0）注册的方法名/argc 与 TSV 一致。重建注册表（src/rgss_data_classes.c:410-532）：五类均缺 initialize_copy 注册，其余方法名与 arity 一致。

按 TSV 全部 94 行（==/===/eql? 各计 1 行，alloc_func 计 1 行）：

| 判定 | 数量 |
|---|---|
| 一致 | 39 |
| 等价 | 9 |
| 偏差 | 27 |
| 桩 | 8 |
| 无法验证 | 11 |
| 未映射 | 0 |
| 合计 | 94 |

分类：Color 18 行（一致 6 / 等价 3 / 偏差 7 / 桩 1 / 无法验证 1）；Tone 18 行（一致 6 / 等价 3 / 偏差 7 / 桩 1 / 无法验证 1）；Rect 19 行（一致 11 / 等价 3 / 偏差 3 / 桩 1 / 无法验证 1）；Table 11 行（一致 4 / 偏差 5 / 桩 2）；Font 28 行（一致 12 / 偏差 5 / 桩 3 / 无法验证 8）。

最高风险三项：
1. **D3+D3b（_dump/_load 二进制格式与 Table 头错位）**——原版存档（Map 数据内嵌 Table、Marshal 的 Color/Rect）不兼容，读档必错。
2. **D2（initialize_copy 五类全缺）**——dup/clone 语义错误，Table#dup 数据丢失。
3. **D1（Color/Tone 钳制全缺）**——超界颜色/色调值在原版被静默钳制，重建原样存储并传入渲染。

次级风险：D4（resize 清数据）、D5 系列（argc/缺参/返回值）、D10/D11/D12（Font setter 不完整）。
