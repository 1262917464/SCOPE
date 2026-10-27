# A2 审计 — Bitmap 类 + Marshal + 类注册链 + 内嵌源码
> 注：本文写于 2026-10-27 仓库重构前；文中 `backup/` `unpacked/` `build/` `test/` `third_party/` 等路径现位于 `local/` 下，`src/rgss_*.c` 现为 `src/runtimes/rgss2/*.c`，`tools/` 已按流水线分桶（unpack/pe/analysis/probe/verify）。


日期：2026-10-27 ｜ 方式：白盒只读审计（objdump 反汇编 + Hex-Rays 缓存 + 原版 DLL 数据段直读）
对象：`src/rgss_bitmap.c`、`src/rgss_marshal.c`、`src/rgss_classes.c`、`src/rgss_embedded.c`
对照：`docs/analysis/rgss2/rgss_ruby_api_map.tsv`（基址 0x10000000）、`build/full_disasm.txt`、`build/orig_pseudo/functions/`、`docs/analysis/rgss2/M5-c_图形类注册链.md`、`src/runtimes/rgss2/embedded/rpg_classes.rb`

判定枚举：一致 / 等价 / 偏差（详述）/ 桩 / 未映射 / 无法验证

## 1. Bitmap 类（TSV 23 条）

TSV 实际 23 条（alloc_func 1 条 + define_method 22 条）；任务描述中提到的
copy_rect / tdraw / brightness / saturation **不在 TSV、也不在原版 Init_Bitmap
(0x10006060) 的注册序列里**，RGSS2 (VX) Bitmap 无这些方法，属任务描述笔误。

原版像素格式（get_pixel 0x10004490 / set_pixel 0x10004550 汇编直证）：
- 位图缓冲 `uint32_t[]`，像素字 = **ARGB8888**（get_pixel 拆包 `shr 0x18→alpha`、
  `shr 0x10→red`、`shr 0x08→green`、低字节→blue；set_pixel/fill_rect 按
  `A<<24|R<<16|G<<8|B` 组包，0x100045f4-0x100046bc、0x100050b3-0x10005150）。
- 像素索引 `pixels[(stride/4)*y + x]`（0x10004508-0x1000451c：
  `mov eax,[esi+0x18]; sar 0x2; imul y; add x`），与重建 `rgss_bitmap.c:179` 一致。
- 越界：get_pixel 置 ecx=0 后走拆包 → **返回 (0,0,0,0)**（0x100044e8 `xor ecx,ecx`
  + 边界检查 0x100044ea-0x10004506）；set_pixel 越界直接跳过写（0x100045ca-0x100045ee）。

| 条目 | 原版地址 | 判定 | 证据摘要 |
|------|----------|------|----------|
| alloc_func | 0x10005FF0 | 等价 | 原版 malloc 0x5C 字节 BitmapImpl 置零 + Ruby 对象绑定 + 链表注册（0x10005ff4/0x10005ffd）；重建 `bitmap_alloc`（rgss_bitmap.c:43-47）calloc 置零 + Data_Wrap_Struct，语义等价、内部布局不同（重建自带 disposed 字段） |
| initialize | 0x10003FA0 | 等价 | 原版双形态：String 参数→文件加载（失败抛 "failed to create bitmap" @0x10103E88，0x10004048-0x10004092）；整数 w,h→校验 w*h≤0x400000（0x10004139 `cmp edx,0x400000; jle`）后建空缓冲；末尾挂默认 Font（0x10004181 call 0x10007a30）。重建 rgss_bitmap.c:73-130 同双形态、同样挂 Font；**偏差**：①加载失败建 1x1 空位图不抛异常（:89-93）②无 0x400000 上限检查。取"等价"（主路径与错误语义保留），两条差异记入偏差清单 B-1 |
| initialize_copy | 0x10004230 | **偏差** | 原版同类校验（"wrong argument class" @0x10103EA4，0x10004281-0x1000428c）+ 深拷贝像素（0x100042f1-0x1000430b 虚表 vtable+4 分配）+ 复制 Font。**重建未注册该方法**：rgss_bitmap.c:1102-1127 的 rgss_init_bitmap 只有 21 个 rb_define_method，无 initialize_copy；dup/clone 得到的 Bitmap 无 impl/pixels，首次访问即崩溃或返回错误数据 |
| dispose | 0x10004CA0 | 等价 | 原版：impl 非零时调 0x10004A70 释放、虚调用 vtable 释放 impl、`[RData+0x8]=0`（0x10004cb9-0x10004cd1）。重建 bm_dispose（rgss_bitmap.c:154-163）free pixels/buf 并置空 + disposed=1，幂等语义一致 |
| disposed? | 0x10011420 | 等价 | 原版按 `[RData+0x8]`(impl) 是否为 0 返回 Qtrue/Qfalse（0x10011436-0x1001143d `neg/sbb/and/add` 魔数序列）。重建按 disposed 标志，二者由 dispose 同步置位，行为一致 |
| width | 0x100043C0 | 等价 | 原版取 impl→buf+0x4，Fixnum 化（`add eax,eax; or 1`，0x100043ee-0x100043f3）。重建 bm_width（:132-137）读 impl->buf->width；差异：原版 disposed 时抛 "disposed bitmap"（0x100043db），重建返回 0（:135）— 记入偏差清单 B-2 |
| height | 0x10004400 | 等价 | 同 width，buf+0x8 + abs() 后 Fixnum 化（0x10004431-0x10004438）。重建 :139-144 一致；同 B-2 差异 |
| rect | 0x10004440 | 等价 | 原版取 w,h 后调 0x1000CB00 建 Rect，把 w/h 写入 +0x10/+0x14（0x1000446f-0x10004489）。重建 bm_rect（:146-152）用 Color/Rect 类四参构造，结果语义相同 |
| blt | 0x10004CE0 | 等价 | 原版 argc 夹取 4..5（0x10004d16-0x10004d34）、默认 opacity=0xFF（0x10004d9b `mov eax,0xff`）、构造 src/dst Rect 后调原生 blit 0x10097000（0x10004e03）。重建 bm_blt（:252-290）scanline 循环 + opacity 缩放 alpha（sa*op/255，:280-285）。像素语义一致（整像素拷贝、alpha 通道搬运）；原生 0x10097000 内部含插值/裁剪细节未逐指令核对，取"等价"（下同，见"无法验证"节） |
| stretch_blt | 0x10004E20 | 等价 | 原版 argc 夹取 3..4、默认 opacity=0xFF（0x10004ec4 `mov eax,0xff`）、调混合器 0x10096680（0x10004f4d）。重建 :292-329 最近邻缩放循环；vop 被读入但 `(void)vop` 丢弃（:305）——原版 opacity 语义部分保留（重建永远全强度）— 记入偏差清单 B-3 |
| fill_rect | 0x10004F70 | 等价 | 原版双形态：Rect→argc≥2 路径（0x10004fbd 取 argv[2]…），x,y,w,h→argc≥5（0x1000500b `push 0x5`）；颜色经 Color double→int 后 `A<<24\|R<<16\|G<<8\|B`（0x100050b3-0x1000514a）调原生 fill 0x10096AD0（0x10005159）。重建 bm_fill_rect（:228-249）仅支持 5 参形式（rb_scan_args "5"，:231），**Rect 单参形式未实现** — 记入偏差清单 B-4；像素组包一致 |
| gradient_fill_rect | 0x10005180 | 等价 | 原版 argc 夹取 3..4（Rect 形）或 6..7（x,y 形，0x100051dd/0x1000527b），末端参数 fill=false 时取 argc-2（0x1000526e `lea eax,[ebx-0x2]`），垂直标志 sete dl（0x1000533b-0x10005341），两端色 ARGB 打包后调原生 0x10016980（0x100054d3）。重建 :1068-1100 仅支持 6 参形式（"61"），无 Rect 形式、无 fill 语义区分（always 两端插值），span=边长-1 与原版插值方向一致 — B-4 同类偏差 |
| clear | 0x100054F0 | 等价 | 原版调原生 fill 0x10096AD0，色=0、Rect=(0,0,w,h)（0x1000551a-0x1000551e `push 0;push 0`）。重建 memset 全零（:199-205），结果一致 |
| clear_rect | 0x10005540 | 等价 | 原版双形态 Rect/4 参（0x10005592 `push 0x1` / 0x100055b9 `push 0x4`），色 0 调原生 fill（0x1000563b）。重建 :207-226 仅 Rect 单参形式（"1"），4 参形式未实现 — B-4 同类偏差 |
| get_pixel | 0x10004490 | 一致 | 边界检查 x∈[0,buf+0x4), y∈[0,abs(buf+0x8))（0x100044ea-0x10004506）；命中读 pixels[(stride>>2)*y+x]（0x10004508-0x1000451c）；拆包 ARGB→Color 四参构造 0x10006B20（0x1000451f-0x10004539）。重建 bm_get_pixel（:171-181）逐项对应：越界返回 make_color(0,0,0,0)，组包 `(px>>16)&FF, (px>>8)&FF, px&FF, (px>>24)&FF` 一致 |
| set_pixel | 0x10004550 | 等价 | 原版：Color 经 0x10009DD0 换类校验，四个 double (Color+0x8/0x10/0x18/0x20) fld/fistp 取整后组 ARGB 写入（0x100045f4-0x100046bc）；越界静默跳过（0x100045ca）。重建 bm_set_pixel（:183-197）越界静默 return Qnil、组包一致；取 rgba 走 rb_funcall(red/green/blue/alpha) 而非直接读 double 字段（经 Ruby 层，值语义等价） |
| hue_change | 0x10005660 | **桩** | 原版：hue>0 时对 impl 调原生 HSL 变换 0x10096920（0x100056c9-0x100056d7：ecx=impl, push hue, push 0,0,0），随后 0x10004980 刷新。重建 bm_hue_change（rgss_bitmap.c:1064）`(void)self;(void)vh; return Qnil;` — **空桩，无算法** |
| blur | 0x100056F0 | **桩** | 原版：call 0x100162D0 原生模糊 + 0x10004980 刷新（0x1000571a-0x10005720）。重建 bm_blur（:1065）`return Qnil;` — **空桩** |
| radial_blur | 0x10005730 | **桩** | 原版：angle/angle 参数取整，fild 压栈后调原生 0x100165E0（0x1000578d-0x10005798）+ 刷新。重建 bm_radial_blur（:1066）空桩 — 注：TSV 无 radial_blur 行，但原版 Init_Bitmap @0x10006231-0x1000623e 注册了（0x10005730, argc=2），重建也已注册同名方法，故仍列为桩 |
| font | 0x10003C90 | 等价 | 原版：disposed 检查（"disposed bitmap" @0x10103DF0）后取 ivar "font"（0x10103E00，0x10003cba-0x10003cc0）。重建 bm_font_get（:1051-1061）同取 "@font"，NIL 时新建默认 Font — 原版无 NIL 兜底（依赖 initialize 已建），行为超集 |
| font= | 0x10003CD0 | 等价 | 原版：disposed 检查 + 参数经 0x10009DD0（Font 类校验）后调 0x10015EA0 把新 Font 的 impl 接到 bitmap 侧并回写 ivar（0x10003d0c/0x10003e09）。重建 bm_font_set（:1062）仅 rb_iv_set — 类校验缺失、无 CFont 状态接管，但 Font 状态本就按方法调用时读取，观察行为一致 |
| draw_text | 0x100057B0 | 等价 | 原版大栈帧（0x2074 字节）解析 Rect/6 参双形态 + align（argc 2..3 / 5..6 夹取，0x10005824-0x10005841/0x100058a4-0x100058bc），字符串 to_s（0x1000586f call 0x100803F0），逐字符度量（0x100059cb/0x10005a1a call 0x100128C0/0x10015C70），最终调原生自绘 0x10015EB0（0x10005b80）。重建 bm_draw_text（:882-1000）双形态 + align 0/1/2 + GetGlyphOutlineW GGO_GRAY8_BITMAP 逐字符光栅化（对应 0x15EB0/0x15CC0 逆向，docs/M5-j），灰度→alpha（64→255 其余×4，:814-816）、阴影 (+1,+1) 两遍合成（:986-990）。UTF-8→UTF-16 经 MultiByteToWideChar(CP_UTF8)（:949-962）。混合器原版 0x96680 模式分发（0x10096680: bit0x4000/0x8000 翻转、0xc0 判 alpha 混合），重建 REPLACE 直写近似 — B-5 |
| text_size | 0x10003E20 | 等价 | 原版：字体 impl→0x100128C0 建字体 + 字符串 to_s + 0x10015C70 逐字符度量，结果写 Rect +0x10=width / +0x14=height（0x10003ea4-0x10003ec1）。重建 bm_text_size（:1003-1048）同源度量函数 rgss_measure_text（:866-879，Σ修正后 gmCellIncX，MS Gothic 20px cell 19→18 修正 :874 对应 0x1614D），返回 Rect(0,0,total,tmH) |
| 注册总数 | — | **偏差** | 原版 Init_Bitmap (0x10006060)：1×rb_define_alloc_func（0x10006078/88）+ **22×rb_define_method**（0x100060a0…0x100062a0，逐条 push fn 地址核对与 TSV 一一对应）。重建 rgss_init_bitmap（rgss_bitmap.c:1102-1127）：1×alloc + **21×method，缺 initialize_copy** |

## 2. Marshal（load_data / save_data）

TSV 无对应行（全局函数不分类）。定位：原版伪代码缓存
`build/orig_pseudo/functions/0x10009a90.c`、`0x10008d90.c`；注册点
`sub_10009E60`（阶段 A）@0x10009eff/0x10009f10。

| 条目 | 原版地址 | 判定 | 证据摘要 |
|------|----------|------|----------|
| load_data | 0x10009A90 | 等价 | 原版伪代码：取 Marshal 模块（sub_10020AD0 aMarshal），参数 1 个；字符串参数时 `File.open(fname,"rb")`（sub_10044020 aRb_0 → "rb"）后 `Marshal.load(f)`（aLoad_0, argc=1），最后 close（sub_10043800）。重建 rgss_marshal.c:18-28 完全同构：File.open(fname,"rb") → Marshal.load → close。注意：原版对非 String 参数走 else 分支（0x10009b0b 直接 Marshal.load）——重建 StringValue() 会抛 TypeError，此处仅覆盖主路径 |
| save_data | 0x10008D90 | **偏差** | 原版伪代码：`File.open(fname,"wb")`（aWb）+ `Marshal.dump(obj, limit?)`（aDump_0，argc=2）+ close，**注册 argc=2**（sub_10009E60 @0x10009f10 `sub_10021310(aSaveData, sub_10008D90, 2)`）。重建 rgss_marshal.c:31-46 实现体等价（File.open "wb" → Marshal.dump → write → close），但 rgss_classes.c:60 注册为 **rb_define_global_function("save_data", …, -1)**（可变参数）且实现 rb_scan_args "21" 接受 3 参。arity 不一致：原版 save_data(obj,fname) 恰 2 参，重建 save_data(obj,fname[,limit]) 1..3 参。功能超集，属偏差（记 B-6） |

## 3. 类注册链（src/rgss_classes.c vs docs/M5-c + 原版反汇编）

| 条目 | 原版地址 | 判定 | 证据摘要 |
|------|----------|------|----------|
| 阶段 A 组成 | 0x10009E60 | 一致 | 原版伪代码 0x10009e60.c：ruby_init(sub_10034910) → eval($KCODE)（word_100FF55E）→ eval(Dir.glob)（aKcodeU/aDirGlob）→ load_data/save_data 注册 → Init_Audio 0x10003B60 → Init_Table 0x1000EFA0 → Init_Color 0x10006950 → Init_Tone 0x10010210 → Init_Rect 0x1000C910 → 尾部 eval 内嵌 blob。重建 rgss_classes.c:41-69 顺序完全一致（Audio→Table→Color→Tone→Rect + load_data/save_data + Zlib + 内嵌 eval）；$KCODE/Dir.glob 在 rgss_export.c:44-45 于 phase A 前执行，顺序等价 |
| 阶段 B 组成 | 0x10009F40 | 一致 | 原版反汇编（full_disasm.txt:12823-12990）：先 rb_define_class("RGSSError")（0x10104484），随后注册全局函数 **p (0x10008DF0, argc=-1)** 与 **print (0x10009B20, argc=-1)**（0x10009f53-0x10009f73），再 3 次 eval（"undef require"/"undef load"/"undef autoload"，@0x10104468/0x1010445C/0x1010444C），最后连续 call：Graphics 0x100084C0 → Bitmap 0x10006060 → Viewport 0x10011090 → Sprite 0x1000E260 → Tilemap 0x1000FB50 → Plane 0x1000C150 → Window 0x100122E0 → Font 0x100075D0 → tail-jmp Input 0x100087B0（0x1000a04c-0x1000a077），与 docs/M5-c 第 1 节逐一吻合。重建 rgss_classes.c:78-98 顺序一致（RGSSError + 9 图形类） |
| 阶段 B 缺项 | 0x10009F51-0x1000A047 | **偏差** | 重建无：① `p`/`print` 全局函数注册（rgss_classes.c phase B 与全 src/ 无 rb_define_global_function("p"/"print")）② 三条 `undef require/load/autoload` eval。游戏脚本若用 p/print 调试输出将 NameError/行为不同 — 记 B-7 |
| Init_Bitmap 方法数 | 0x10006060 | **偏差** | 原版 22 方法 vs 重建 21（缺 initialize_copy，见 §1 末行）— 记 B-8 |
| 两阶段触发时机 | — | 一致 | 重建 phase A 于 RGSSInitialize2（rgss_export.c:46）、phase B 于 RGSSGameMain 开头（rgss_gamemain.c:250，对应原版 CGameFrame::Run 首步），与 docs/M5-c 的延迟注册模型一致 |

## 4. 内嵌源码注入（src/rgss_embedded.c vs src/runtimes/rgss2/embedded/rpg_classes.rb）

| 条目 | 原版地址 | 判定 | 证据摘要 |
|------|----------|------|----------|
| 内嵌 blob 内容 | 0x100FF650 | 一致 | 原版 DLL `unpacked/RGSS202E_unpacked.dll` 文件偏移 0xFF650 起的 NUL 结尾 C 串，长度 **18213 字节**，与 `src/runtimes/rgss2/embedded/rpg_classes.rb`（18213 字节）**逐字节相等**（py 比对 exact equal: True），`module RPG;class Map;def initialize(width, height);…` 起始一致 |
| 注入时机/方式 | 0x10009E60 末尾→0x1000345C | 等价 | 原版：阶段 A 末尾 sub_100330A0(off_1015B044[0]→0x100FF650) eval（0x10009e60.c 末行）。重建 rgss_embedded.c:12-24：objcopy 符号取 start/end，malloc 拷贝 + rb_eval_string_protect，在 rgss_classes.c:66-68 phase A 末尾调用，时机一致。差异：原版经 eval helper（sub_10008A80，3 参形式），重建 rb_eval_string_protect 直调；失败时重建静默返回 0，无异常上抛 — 观察面等价 |
| 注入内容大小上限 | — | 等价 | 重建 len>1MB 拒绝（:15），原版 blob 18KB 远小于上限，无实际影响 |

## 5. 无法验证 / 覆盖说明

1. **原生图形核心 0x10097000 (blt)、0x10096680 (blender)、0x10096AD0/0x10096920 (fill/hue)、0x10016980 (gradient)、0x100162D0 (blur)、0x100165E0 (radial_blur)、0x10015EB0/0x10015CC0 (draw_text/text_size 核心)**：本次核对了入口参数、模式分发与调用约定，未逐指令核对全部像素数学（如 blt 的双线性插值、blur 核大小）。其中 blur/radial_blur/hue 在重建侧为空桩，无需数学核对；blt/stretch_blt/fill 的重建实现为语义近似（已逐条标注）。0x15EB0/0x15CC0 的字形光栅化细节已由 docs/M5-j 单独逆向并回填重建，本次抽查常量（GGO_GRAY8_BITMAP=6、64→255 灰度映射、cell 19→18 修正、MAT2 单位阵 @0x15CEB/0x15FC8）均吻合。
2. **原版 sub_10009A90 load_data 的非 String 分支**：伪代码显示 else 路径直接 Marshal.load（IO 语义），重建未覆盖；RGSS 游戏脚本只用文件名路径，实际影响低。

## 6. 汇总

判定计数（共 33 条：Bitmap 23 + Marshal 2 + 注册链 5 + 内嵌 3）：

| 判定 | 数量 | 条目 |
|------|------|------|
| 一致 | 4 | get_pixel、内嵌 blob 内容、阶段 A 组成、阶段 B 组成（顺序） |
| 等价 | 24 | alloc_func、initialize、dispose、disposed?、width、height、rect、blt、stretch_blt、fill_rect、gradient_fill_rect、clear、clear_rect、set_pixel、font、font=、draw_text、text_size、load_data、两阶段触发时机、内嵌注入时机、内嵌大小上限、（width/height 按主语义）、（save_data 实现体单独计偏差） |
| 偏差 | 4 | initialize_copy（缺注册）、save_data（arity 2 vs -1）、阶段 B 缺 p/print+undef、Init_Bitmap 方法数 22 vs 21 |
| 桩 | 3 | hue_change、blur、radial_blur |
| 未映射 | 0 | load_data/save_data 已通过伪代码缓存映射（TSV 无行但已定位） |
| 无法验证 | 1 类 | 原生像素核心内部数学（见 §5，不单列条目） |

### 偏差清单（P1→P3）

| # | 级别 | 条目 | 内容 |
|---|------|------|------|
| B-1 | P2 | initialize | 加载失败建 1x1 空图（应抛 "failed to create bitmap"）；无 w*h≤0x400000 上限检查（0x10004139）。src/rgss_bitmap.c:89-93 |
| B-2 | P3 | width/height | disposed 时原版抛 "disposed bitmap"（0x100043db），重建返回 0。src/rgss_bitmap.c:135,142 |
| B-3 | P2 | stretch_blt | opacity 参数被丢弃（`void vop`），原版传给混合器 0x10096680（0x10004ec4 默认 0xFF）。src/rgss_bitmap.c:305 |
| B-4 | P2 | fill_rect/clear_rect/gradient_fill_rect | Rect 单参形式未实现（fill_rect 原版 argc 2..6 双形态 0x10004fbd/0x1000500b；clear_rect 1..4 双形态 0x10005592/0x100055b9；gradient 同理），重建 rb_scan_args 仅收固定形式 |
| B-5 | P3 | draw_text | 合成为 REPLACE 直写近似，原版混合器 0x96680 支持更多 blend 标志位；阴影/正文两遍合成已还原 |
| B-6 | P2 | save_data arity | 原版 argc=2（0x10009f10），重建注册 -1 + 实现 1..3 参（rb_scan_args "21"）。src/rgss_classes.c:60、src/rgss_marshal.c:35 |
| B-7 | P2 | 阶段 B 缺项 | p / print 全局函数（0x10008DF0/0x10009B20）与 undef require/load/autoload 三条 eval（0x10009f51-0x1000a047）未重建 |
| B-8 | P1 | initialize_copy 缺失 | 原版 0x10004230 + "wrong argument class" 校验 + 深拷贝；重建 rgss_init_bitmap 未注册该方法，dup/clone 产物不可用 |
| B-9 | P2 | 桩 | hue_change (0x10005660→0x10096920)、blur (0x100056F0→0x100162D0)、radial_blur (0x10005730→0x100165E0) 三个空桩，原版均有完整原生实现 + 刷新调用 0x10004980 |
