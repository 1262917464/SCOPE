# A5 白盒审计 — Input / Audio / 生命周期导出 / 主循环
> 注：本文写于 2026-10-27 仓库重构前；文中 `backup/` `unpacked/` `build/` `test/` `third_party/` 等路径现位于 `local/` 下，`src/rgss_*.c` 现为 `src/runtimes/rgss2/*.c`，`tools/` 已按流水线分桶（unpack/pe/analysis/probe/verify）。


日期: 2026-10-27 ｜ 方式: 重建源码 (src/) vs 原版反汇编 (build/full_disasm.txt, 基址 0x10000000) + Hex-Rays 缓存 (build/orig_pseudo/functions/)
范围: TSV class ∈ {Input, Audio} 全部条目、rgss_export.c 生命周期导出、rgss_gamemain.c 主循环、rgss_regex_stubs.c 正则导出。只读审计, 未改动 src/。

判定枚举: 一致 / 等价 / 偏差（详述）/ 桩 / 未映射 / 无法验证。

---

## 1. Input (TSV 6 条 + 内部函数 3 个)

### 1.1 公开包装函数

| 条目 | 原版地址 | 判定 | 证据摘要 |
|------|----------|------|----------|
| Input.update | 0x100086c0 | 偏差 | 原版: `mov ecx,[game+0x25c]; call 0x10013c40` → CInput::Update。重建 (rgss_graphics_classes.c:280-359) 状态机骨架一致 (prev←cur、采样、连发、dir4/8)，但内部存在 §1.3 所列偏差 |
| Input.press? | 0x100086e0 | 一致 | 原版 0x10013680: `cmp eax,0x1d; ja →0; movzx eax,[ecx+key+0x4f]` (0..29 边界, 读当前帧 +0x4f)。重建 input_press_p (L361-367) 相同; 重建额外排除 key==0, 但原版槽位 0 恒为 0 (0x10013c2c 显式清零), 语义等价 |
| Input.trigger? | 0x10008710 | 一致 | 原版 0x100136a0: `cur[key+0x4f]!=0 && cur[key+0x6d]==0`。重建 input_trigger_p (L369-376) 相同 |
| Input.repeat? | 0x10008740 | 一致 | 原版 0x100136d0: `key==[ecx+0x8c] && (count==1 \|\| (count>=0x18 && count%6==0))`。重建 input_repeat_p (L378-389) 完全一致 (计数==1 或 ≥24 且 %6==0 ✓) |
| Input.dir4 | 0x10008770 | 偏差 | 原版 0x10013710 仅 `mov eax,[ecx+0x94]`; 数值本身由 0x10013d65 段计算, 该段为**末写覆盖序** (down>right>up>left), 重建 (L322-327) 为竖直优先 → 冲突键结果不同 (见 §1.3-D) |
| Input.dir8 | 0x10008790 | 偏差 | 原版 0x10013720 仅读 [ecx+0x98]; 原版 0x10013d3c-0x10013deb 有 +0x9c **对角迟滞** (上一次直线方向轴决定斜向归并), 重建无 → 斜向 dir8 序列不同 (见 §1.3-C) |

注: 包装层参数转换 (test al,1 → FIXNUM>>1, 否则 0x100567d0=NUM2LONG) 与返回值编码 (neg/sbb/and 2 → Qtrue=2/Qfalse=0; dir4/8 → jmp 0x1001d850=INT2FIX) 重建均一致。

### 1.2 键号常量与 CInput 布局

- 常量注册段 0x100087b0 起: 定义 6 方法 (argc 0/1/1/1/0/0) + 21 个常量, 值序列 4,8,6,2,0xB,0xC,0xD,0xE,0xF,0x10,0x11,0x12,0x15,0x16,0x17,0x19,0x1A,0x1B,0x1C,0x1D。重建 enum (L191-201) 与 rgss_init_input consts (L898-910) **逐值一致** ✓
- CInput 布局 (+0x4f 当前/+0x6d 上一帧/+0x8c 连发键/+0x90 连发计数/+0x94 dir4/+0x98 dir8/+0x9c 斜向迟滞标志) 与重建注释一致; 重建用独立数组模拟, 布局等价。原版 0x10013c4a-0x10013c81: `rep movs` ×3 (cur→prev, sample→cur, 清 sample) 与重建 memcpy 语义一致

### 1.3 逐键 VK 映射对照表 (重点)

原版采样函数 0x10013940 (GetAsyncKeyState, IAT 0x100ff2d4, `test ax,ax; jl` = 高位 pressed) + 手柄 joyGetPosEx (IAT 0x100ff3dc, JOYINFOEX dwSize=0x34/dwFlags=0xFF, 阈值 (min+2·max)/3 由 0x10013510 标定)。原版按键→槽位 (槽位 = 偏移-0x31, 即键号):

| 键号 (常量) | 原版物理键 (0x10013940 实测 VK) | 重建 g_key_vk / rgss_poll_key (L204-277) | 判定 |
|---|---|---|---|
| 2 DOWN | 0x28 VK_DOWN, 0x62 NUMPAD2 | VK_DOWN, NUMPAD2 | 一致 |
| 4 LEFT | 0x25 VK_LEFT, 0x64 NUMPAD4 | VK_LEFT, NUMPAD4 | 一致 |
| 6 RIGHT | 0x27 VK_RIGHT, 0x66 NUMPAD6 | VK_RIGHT, NUMPAD6 | 一致 |
| 8 UP | 0x26 VK_UP, 0x68 NUMPAD8 | VK_UP, NUMPAD8 | 一致 |
| 11 A | **0x10 VK_SHIFT** (10013afd/bd0) | **'Z' (0x5A)** | **偏差 A/C 交换**: 原版 Shift→A; 重建物理 Z→A, 且 Shift→A 完全缺失 |
| 12 B | 0x1B VK_ESCAPE, 0x60 NUMPAD0, 0x2D VK_INSERT, 0x58 'X' (10013ad3/ae8/af9/b44) | 'X' + Esc (L262-263) | 偏差: 重建缺 NUMPAD0/INSERT→B |
| 13 C | 0x5A 'Z', 0x0D VK_RETURN(**且 !Alt**), 0x20 VK_SPACE(**且 !Alt**) (10013aa8/ac2/b2f) | 'C' (0x43) + Enter + Space (L258-260) | **偏差 A/C 交换**: 原版 Z→C; 重建物理 'C'→C。且原版 0x10013b4f 处 0x43 'C' 映射到槽位 0 (**显式废弃**), 重建把废弃键激活了; 重建还缺 !Alt 排除 (原版避免 Alt+Space/Enter 系统菜单) |
| 14 X | 0x41 'A' | 'A' | 一致 |
| 15 Y | 0x53 'S' | 'S' | 一致 |
| 16 Z | 0x44 'D' | 'D' | 一致 |
| 17 L | 0x51 'Q', 0x21 VK_PRIOR (10013b17) | 'Q' + VK_PRIOR | 一致 |
| 18 R | 0x57 'W', 0x22 VK_NEXT (10013b23) | 'W' + VK_NEXT | 一致 |
| 21 SHIFT | 0x10 VK_SHIFT (10013bd9→+0x46) | VK_LSHIFT\|VK_RSHIFT | 等价 (VK_SHIFT 本就覆盖左右) |
| 22 CTRL | 0x11 VK_CONTROL | VK_LCONTROL\|VK_RCONTROL | 等价 |
| 23 ALT | 0x12 VK_MENU | VK_LMENU\|VK_RMENU | 等价 |
| 25-29 F5..F9 | 0x74..0x78 (10013bfd..10013c32) | VK_F5..VK_F9 | 一致 |
| 手柄 | joyGetPosEx 按钮 0-24 位域, 经 ctor 0x100138a0 的 +0x18..0x30 映射表 (bit0-7→A,B,C,X,Y,Z,L,R; bit10-16→C,C,B,B,A,C,B; bit20-24→X,Y,Z,L,R) | 无任何实现 | 桩 (无手柄支持) |
| 手柄摇杆→方向 | X<[esi+8]→LEFT, X>[esi+0xC]→RIGHT, Y<[esi+0x10]→UP, Y>[esi+0x14]→DOWN (10013978-1001399d) | 无 | 桩 |

### 1.4 状态机算法偏差

| # | 项目 | 原版证据 | 重建 | 影响 |
|---|------|----------|------|------|
| A | A/C 键映射交换 + Shift→A 缺失 | 0x10013940: Shift→A(11), Z/Enter/Space→C(13), 'C' 键→槽 0 废弃 | g_key_vk[11]='Z', g_key_vk[13]='C' | **会串扰**: 物理 Z 触发 Input::A (原版触发 C); 物理 C 触发 Input::C (原版无效果); Shift 不再产生 A (疾跑等失效) |
| B | 连发键扫描 tie-break | 0x10013c90-0x10013cb3: 扫描 0..0x1D **不 break**, 最后一个新键胜出 | L293-295: 找到第一个新键即 break | 同帧多键新按时连发键选择不同 (低危) |
| C | 对角迟滞 (+0x9c) | 0x10013dcf-0x10013de6: 斜向时读 [ebx+0x9c]; ==0 → edx=0 (纵向抑制), ==1 → esi=0 (横向抑制); 直线移动时置 1 (0x10013deb)/清 0 (0x10013d5f) | L307-328 无迟滞, 斜向恒报对角 | dir8/dir4 斜向序列与原版不同: 如从纯右转斜上, 原版先继续 6 (dir8) 再视迟滞归并, 重建直接 9。**这会表现为"方向跳变", 黑盒观感近似按键串扰** |
| D | dir4 冲突优先级 | 0x10013d65-0x10013d9f: 左(4)→上(8)→右(6)→下(2) 顺序**覆盖写**, 末写胜 → down>right>up>left; 例: 上+右 → **6**; 左+右 → **6** | L322-327: 竖直优先 if/else; 上+右 → **8**; 左+右 → **0** | 冲突键时 dir4 值不同 (上+右最常见, 影响斜向移动的 dir4 语义) |
| E | 手柄/摇杆 | joyGetPosEx 全套 (0x10013510 标定, 0x10013940 采样) | 无 | 手柄不可用 (低危, 视需求) |

### 1.5 Input 结论

重建 Input **存在可能导致按键串扰的实现差异**: 核心是 (1) A/C 主键映射交换 (物理 Z↔Input::A、物理 C↔Input::C、Shift 不再映射 A), (2) 对角迟滞缺失 + dir4 覆盖序不符导致斜向移动方向值与原版不一致。press?/trigger?/repeat? 的计数与状态机逻辑本身与原版逐指令级一致。

---

## 2. Audio (TSV 11 条)

| 条目 | 原版地址 | 判定 | 证据摘要 |
|------|----------|------|----------|
| bgm_play | 0x100035a0 | 桩 | 原版 (pseudo 0x100035a0.c): argc 1..3 校验 (越界 sub_10008F20 抛错), volume/pitch 默认 100, 文件名经 sub_10009440 RTP 解析, 单例 dword_1016EFE8 非空才调 sub_10095050; 重建 audio_play (L395) 直接 return Qnil |
| bgm_stop | 0x10003460 | 桩 | 原版: 单例非空 → sub_100951A0; 重建 no-op (L396) |
| bgm_fade | 0x10003480 | 桩 | 原版: NUM2INT → sub_10095100(sec); 重建 audio_fade no-op (L397) |
| bgs_play | 0x10003710 | 桩 | 同 bgm_play 模式 → sub_100951C0; 重建 no-op |
| bgs_stop | 0x100034c0 | 桩 | 原版 → sub_100951A0? 实测 0x100034c0.c → sub_100951A0(bgs 版); 重建 no-op |
| bgs_fade | 0x100034e0 | 桩 | 原版 → sub_100951B0; 重建 no-op |
| me_play | 0x10003880 | 桩 | 原版 → sub_100951C0(me 版), 同样 argc 校验+RTP 解析; 重建 no-op |
| me_stop | 0x10003520 | 桩 | 原版 → sub_10095260; 重建 no-op |
| me_fade | 0x10003540 | 桩 | 原版 → sub_100952C0; 重建 no-op |
| se_play | 0x100039f0 | 桩 | 原版 → sub_10095380 系; 重建 no-op |
| se_stop | 0x10003580 | 桩 | 原版 → sub_100953A0; 重建 no-op |

桩与原版语义差距:
1. 原版 play 系对 argc<1 / >3 抛 ArgumentError (sub_10008F20), 对播放失败 (v11==0) 置 ruby_errinfo=2 并调 sub_10026120 (文件缺失报错); 重建对任意参数静默接受。
2. 原版文件名经 RTP 路径解析 (sub_10009440), 找不到 RTP 文件会抛错; 重建永不报错。
3. 原版 stop/fade 均以 CSxAudio 单例 (0x1016EFE8) 存在为前提; 重建无单例。
4. 注册面一致: rgss_init_audio (L929-944) 11 个方法名/argc 与原版 sub_10003B60 完全一致。

---

## 3. 生命周期导出 (rgss_export.c) 与主循环 (rgss_gamemain.c)

### 3.1 生命周期导出

| 条目 | 原版地址 | 判定 | 证据摘要 |
|------|----------|------|----------|
| RGSSInitialize2 | 0x100027f0 | 偏差 | 原版: `call sub_10009E60` (完整引擎初始化, 含全部类注册 3B60/EFA0/6950/10210/C910) + hwnd→0x1017347C + 返回 a1。重建 (L34-48): ruby_init/ruby_script + 存 hwnd + eval "$KCODE='U'" + **"Dir.glob('.')" (调试残留)** + rgss_register_phase_a; Graphics/Sprite 等类注册被推迟到 GameMain 的 phase B (rgss_gamemain.c:250)。返回值一致 (hwnd) |
| RGSSFinalize | 0x10002800 | 一致 | 原版: `dword_10173478=0`; 重建 L52-55: g_running=0 (同语义) |
| RGSSGameMain | 0x10002890 | 偏差 | 原版: 先 eval(加密串@0x100ff5d8) 结果==Qtrue(2) → 置 $TEST 标志 0x1016EFE0 (M2 文档: ENV TEST==2); new CGameFrame(0x26C, ctor 0x10001020) → 0x1016EFE4; ret=frame->Run(0x10001F00)→内含 sub_1000AFD0; 虚析构; 返回 ret。重建 (rgss_gamemain.c:240-304): **无 $TEST eval**; 自建窗口 (或采用宿主窗口); 返回恒 0; 其余见 §3.2 |
| RGSSEval | 0x10002910 | 一致 | 原版: eval_protect(0x100330a0) 返回 state; 重建 L62-67 相同 |
| RGSSGetBool | 0x10002930 | **偏差(反转)** | 原版: `cmp eax,2; sete dl` → **eval==Qtrue(2) 才返回 1**。重建 L70-75: `return !st && v == Qfalse` → **eval==Qfalse 才返回 1**, 真值判断反转。注: docs/M2_Game.exe接口文档.md:37 "eval 结果==Qfalse(2) 判假" 系文档笔误 (Ruby 1.8 Qtrue=2/Qfalse=0, 与 Input 包装 neg/sbb/and 2 的编码互证) |
| RGSSGetInt | 0x10002d70 | 等价 | 原版: Fixnum>>1 / Float(flags&0x3f==0xa)→rb_num2int / 其它特殊值→raise 或 0。重建 L77-85: FIXNUM/FLOAT→int, 其它→0。差异仅在异常类型抛出 (原版对 Table 等抛错, 重建返 0) |
| RGSSGetDouble | 0x10002e00 | 等价 | 原版: 同上模式, 失败 fldz; 重建返 0.0, 一致 |
| RGSSGetTable | 0x10002960 | 桩 | 原版: eval 后 TYPE==0x22(T_DATA) → out[0..3]=[+0xC,+0x10,+0x14,+0x1C]。重建 L99-107: 恒清零, TODO 未实现 |
| RGSSGetStringUTF8 | 0x10002e70 | 偏差 | 重建 L120: `if (st \|\| !TYPE(v) == T_STRING)` — 运算符优先级 bug, 实为 `(!TYPE(v))==T_STRING` 恒假, 类型检查失效; 非 String 结果会走 RSTRING(v)->ptr (未定义行为)。取值路径 (lstrcpynA 到 4096 静态缓冲) 与原版 rb_str_ptr 语义近似 |
| RGSSGetStringACP | 0x10002ed0 | 偏差 | 原版: UTF8→UTF16→ACP 双转。重建 L125-134: 把 WideCharToWideChar 的**宽字节结果写进 char buf 后按 char\* 返回**, 编码/缓冲完全错乱; 且同上类型检查 bug |
| RGSSGetStringUTF16 | 0x10002ff0 | 等价 | 重建 L136-145: MultiByteToWideChar(65001)→wchar 静态缓冲, 与原版一致 (类型检查 bug 同上) |
| RGSSSetString/UTF8/ACP/UTF16 | 0x100030e0/3180/3220/3350 | 等价 | 原版: sprintf 到全局缓冲 + eval + rb_str 内容替换。重建: eval "var='val'" (值含引号会坏), ACP/UTF16 先转 UTF8 ✓。近似等价 |
| RGSSErrorType | 0x100029c0 | 等价 | 原版: 每次 memset(0x1016F7F8,0x400) 后返回指针; 重建 L176 返回静态缓冲**不清零** (可能残留旧错)。ErrorMessage 0x100029e0 → 0x1016FFF8 同理。且重建两缓冲从未被写入 |
| RGSSSetupRTP | 0x10002aa0 | 桩 | 原版 (3 参数): RGSSClearRTPPath → RegOpenKeyExW(HKLM, key@0x100ff600) → 循环 RTP/RTP1..RTP9 (SHGetValueW@0x100ff1e4, buf 0x800) → 去尾 '\\' → AddRTPPath(0x10009080) + **逐路径调用 SetupFonts(0x10009110)** → RegCloseKey, 返回 1。重建 L181: `return 1;` 纯桩, 注释"Game.exe 实测无参"与原版 3 参数签名不符。副作用: RTP 字体注册链整段缺失 |
| RGSSAddRTPPath/ClearRTPPath/GetRTPPath | 0x10002d30→0x10009080 / 0x10002d40→0x100090d0 / 0x10002d20→0x100090e0 | 桩 | 重建均 return 0, 原版为真实路径表管理 |
| RGSSGetPathWithRTP | 0x10002a10 | 桩 | 原版: 在 RTP 路径表中解析 name (<260) 命中返回全路径; 重建 L187-193 原样拷回 name |
| RGSSSetupFonts | 0x10002d50 → sub_10009110 | 偏差(可接受) | 原版导出: `push [0x100ff63c]; call 0x10009110` — 路径来自全局。sub_10009110 (pseudo): path + L"\\Fonts\\*" (0x101043AC) → FindFirstFileW → 非目录 → path+L"\\Fonts\\"+cFileName → AddFontResourceExW(p, **16=FR_PRIVATE**, 0) → FindNextFileW/FindClose。重建 L204-241: 机制完全一致 (FindFirstFileW/FR_PRIVATE/\\Fonts\\*), 但路径取 GetModuleFileNameA(exe 目录) 而非全局 0x100ff63c; 3 形参被忽略 |
| RGSSAudioInitialize | 0x10002810 | 桩 | 原版: 若旧单例在先虚析构 → malloc(0xA4?? 实为 CSxAudio 0x164 布局的分配 0x100dc16a) → ctor 0x10095920 → init(hwnd 参数) 0x10095490 → 存 0x1016EFE8。重建 L244: `return 0;` |
| RGSSAudioFinalize | 0x10002870 | 等价 | 原版: 单例非空→虚析构+清零; 重建无单例可清, return 0 (与"无音频"现状一致) |
| RGSSGC | 0x10002d60 → jmp 0x1003f2e0 | 等价 | 原版为 rb_gc_start 转发; 重建 L246 rb_gc_start + return 0 (返回值编码可能不同, 低危) |

### 3.2 主循环 (原版 sub_1000AFD0, RGSSGameMain→CGameFrame::Run 0x10001F00 内调)

| 项目 | 原版 (sub_1000AFD0 + M5_主循环与Init归属.md 互证) | 重建 (rgss_gamemain.c) | 判定 |
|------|------|------|------|
| Game.ini 读取 | scripts_fname 为入参 (上层经 Game.ini [Game] Scripts 取得, sub_10008C70 做编码转换) | GetPrivateProfileStringA("Game","Scripts") L71-81 | 等价 |
| $RGSS_SCRIPTS 装载 | eval "$RGSS_SCRIPTS = load_data(...); each{s[3,0]=Zlib::Inflate.inflate(s[2])}; size" (0x1000b145-0x1000b194) | 同一片段 (L102-105) | 一致 |
| 逐段 eval 顺序 | i: eval("'Section%03d'") (0x1000b252) → T_STRING 校验 → eval("$RGSS_SCRIPTS[%d][3]") (0x1000b292) → 0x10173478=1 → eval(src) → =0 (0x1000b2c1-0x1000b2f1) | 同序 (L128-173), running 标志对应 g_running | 一致 |
| F12 = Reset | 0x1000b0f6: `while(GetAsyncKeyState(123)&0x8000){ pump(0x10001640); alive?(0x10001890)→else exit }`; Reset 异常由窗口过程抛出, 卸到本函数 SEH, "$!.type.to_s"=="Reset" → continue 大循环 (0x1000b30d-0x1000b340) | wndproc 只置 `g_reset_requested` (L37), **该标志从未被读取 (L289 置 0 后无人消费), 也无任何 rb_raise("Reset")** → F12 重启完全无效, 仅当脚本自身抛名为 Reset 的异常时重启逻辑 (L185-193) 生效 | 偏差 (功能缺失) |
| $TEST/$BTEST | RGSSGameMain 序言 eval 后 ==Qtrue → 0x1016EFE0 (M2 文档: ENV TEST==2) | 无任何实现 (g_test_mode 声明未用, rgss_export.c:15) | 偏差 (低危) |
| 错误弹窗 | "$!.type.to_s"!="Reset" → sprintf 错误 (sub_1000A080) → [frame+580] 虚调用 + sub_100015A0 弹窗 → return | $! 类名/消息 + backtrace 日志 + MessageBoxA (L185-209) | 等价 |
| 退出清理 | 正常路径: 6 个原生清理 sub_100048F0/10010EA0/1000E080/1000F970/1000BF70/10012100 + eval("Graphics._reset;Input.update;Audio.me_stop;Audio.se_stop;Audio.bgm_stop;Audio.bgs_stop;GC.start;") (0x1000b3c8-0x1000b3eb) | eval 同一串 (L298-300) ✓; 6 个原生清理无对应 (重建无原生场景栈, 可接受); 差异: 重建在**脚本装载失败路径也会执行**该清理 (原版错误路径提前 return 不清理) | 等价(偏差低危) |
| 帧率限制 | 原版不在主循环, 在 Graphics.update 链: 0x10007b10→0x10001640 (timeGetTime@0x100ff3d4 帧跳过器: 间隔=[frame+0x214], 跳帧计数 +0x22c, >0.64s? 重置) | gfx_update 内 QPC+Sleep 限帧 (rgss_graphics_classes.c:64-89) | 等价 (机制不同: 原版跳帧不追赶, 重建 QPC 对齐) |
| 窗口存活检查 | Graphics.update 尾: frame->alive?(0x10001890)==0 → rb_exit(0x10033050, 串@0x100ff598) | 无 (窗口销毁后继续跑) | 偏差 (低危) |

---

## 4. 正则/编码导出 (rgss_regex_stubs.c)

导出表核对: 原版 69 名 (build/old_exports.txt) vs 重建 (new_exports.txt) **名称与顺序 1:1 完全一致** (diff 逐名相同)。构成: RGSS\* 26 (rgss_export.c 实现) + EncDefaultCharEncoding/Reg\* 7 + enc_get_\* 3 + re_alloc_pattern 1 + regex_\* 23 + ruby_re_\* 9 = 69 (任务书"30+39"实际为 26+43)。

| 组 | 数量 | 判定 | 证据摘要 |
|----|------|------|----------|
| ruby_re_compile/search/match/free_pattern/free_registers/adjust_startpos/mbcinit/set_casetable | 8 | 等价 | 由 Ruby 1.8.1 静态库 regex.c 原生提供 (与原版同源的 GNU regex), .def 真实导出 |
| ruby_re_recompile_pattern | 1 | 桩 | 原版为 Enterbrain 扩展; 重建返回 0 不重编译 (rgss_regex_stubs.c:104-110) |
| regex_new | 0x1007C2F0 | 桩 | 原版: 真实编译链 regex_new(0x1007C2F0)→sub_1007C230→sub_1007BF30 (分配+GNU regex 编译, 失败码 -5/-0xB); 重建 (L55-63) 恒返回 0 不编译 |
| regex_search/match | — | 桩 | 原版真实执行匹配; 重建恒返回 -1 (L66-70) |
| regex_free / region_* 6 个 / init/end/version / get_* 3 个 / recompile / name_to_* 2 个 / number_of_names / foreach_name / error_code_to_str / set_warn_func 2 个 | 18 | 桩 | 重建全部空实现 (L65-89); 原版为真实封装层 (0x1007Bxxx 区) |
| enc_get_left/right_adjust_char_head, enc_get_prev_char_head | 3 | 桩 | 重建仅做 p-start 指针运算, 无编码表 (SJIS/EUC-JP 多字节边界会错); 原版基于编码表 |
| EncDefaultCharEncoding / RegDefaultSyntax / RegSyntaxRuby / RegEncoding{ASCII,UTF8,EUC_JP,SJIS} | 7 | 无法验证 | 数据导出: 重建返回静态字符串/NULL 占位 (L20-39); 原版为数据对象, 未逐字节比对 |

结论: Ruby 层 Regexp 功能因走 Ruby 静态库自身 regex.c 而可用; 但 43 个导出中约 27 个封装层导出为**语义空洞的桩**, 若外部宿主直接调用这些导出将与原版行为不符。

---

## 5. 汇总

### 判定计数 (共 74 项)
- 一致: 6 (press?/trigger?/repeat?/Finalize/Eval/GameMain 的 eval 片段与逐段顺序按分项计)
- 等价: 10 (SHIFT/CTRL/ALT 采样、GetInt、GetDouble、GetStringUTF16、SetString×4、ErrorType/Message、AudioFinalize、GC、ruby_re_\*×8、帧率限制、Game.ini 读取、错误弹窗 等)
- 偏差: 10 (详见下方清单)
- 桩: 29 (Audio×11、GetTable、SetupRTP、AddRTPPath、ClearRTPPath、GetRTPPath、GetPathWithRTP、AudioInitialize、ruby_re_recompile_pattern、regex_new/search/match 等 18 封装、enc_get_\*×3)
- 无法验证: 7 (Enc/Reg 数据导出)
- 未映射: 0

### 偏差清单 (按危害排序)
1. **Input A/C 键映射交换** (rgss_graphics_classes.c:216-218,258-263 vs 0x10013940): 物理 Z→Input::A (原版→C)、物理 C→Input::C (原版废弃)、Shift→A 缺失。★按键串扰根因之一
2. **对角迟滞缺失** (L307-328 vs 0x10013dcf-0x10013de6, CInput+0x9c): 斜向 dir8/dir4 归并序列与原版不同。★"方向跳变"观感
3. **dir4 冲突覆盖序不符** (L322-327 vs 0x10013d65-0x10013d9f): 原版末写覆盖 down>right>up>left (上+右→6), 重建竖直优先 (上+右→8)
4. **RGSSGetBool 真值反转** (rgss_export.c:74 vs 0x1000294c: `cmp eax,2; sete`)
5. **F12 Reset 死代码** (rgss_gamemain.c:37 g_reset_requested 无人消费, 无 rb_raise("Reset"))
6. **RGSSGetStringACP 返回宽字节按 char\*** (rgss_export.c:132-133); **GetStringUTF8/ACP/UTF16 类型检查 `!TYPE(v)==T_STRING` 恒假** (L120/131/142)
7. **RGSSSetupRTP 纯桩** (rgss_export.c:181 vs 0x10002aa0 注册表 RTP1..9 + 逐路径 SetupFonts)
8. **$TEST 标志未实现** (rgss_export.c:15 未用 vs 0x100028a2-0x100028b4)
9. **连发键扫描 tie-break 相反** (rgss_graphics_classes.c:293-295 vs 0x10013c90 不 break)
10. **B 键缺 NUMPAD0/INSERT**, **C 键缺 !Alt 排除** (0x10013ad3/ae8/af9, 10013aa6/ac0)

### 对"重建 Input 是否存在按键串扰"的明确回答
**是。** 重建把 RGSS 键号 A(11) 映射到物理 Z、C(13) 映射到物理 C, 而原版是 A(11)=Shift、C(13)=Z/Enter/Space 且物理 C 键被显式废弃 (0x10013b4f 写入槽 0 后于 0x10013c2c 清零)。结果是: 按物理 Z 会同时点亮脚本眼里的 Input::A (原版点亮 Input::C), 按物理 C 触发原版不该有的 Input::C, 按 Shift 则 Input::A 无反应。此外对角迟滞 (+0x9c) 与 dir4 末写覆盖序的缺失, 使斜向移动时 dir4/dir8 输出序列与原版不同, 黑盒观感上加重"按键互相干扰"的错觉。press?/trigger?/repeat? 的帧语义与连发计数 (==1 或 ≥24 且 %6==0) 本身与原版逐指令级一致, 不是串扰来源。
