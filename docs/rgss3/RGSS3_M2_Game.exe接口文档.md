# RGSS3 M2 — Game.exe ↔ RGSS300.dll 接口文档

> 依据：27 个导出（exports.txt）+ REA/IDA 9.4 headless Hex-Rays 反编译（**27/27 成功**，
> 另含引擎初始化/主循环等 4 个核心内部函数）。地址均为 VA（基址 0x10000000），
> 对应 dump 为 `unpacked/RGSS3_300_pristine`（= RGSS3_300_probe.dll）。

## 调用契约（Game.exe 侧典型流程）

```
hInst ← WinMain
RGSSInitialize3(hInst)                       // 注意：3 代传 HINSTANCE（2 代传 hwnd）
RGSSSetupFonts()
RGSSSetupRTP(Game.ini路径, buf, size)        // 遍历 ini [Game] RTP/RTP1..RTP9 → 注册表解析 → AddRTPPath
RGSSAudioInitialize(hwnd)
RGSSGameMain(hwnd, Scripts路径wchar*, flags) // ⚠ 直接 exit(ret) 结束进程，不返回 Game.exe
（GameMain 内部完成主循环 + 收尾，进程退出）
```

## 0. 关键共享原语（所有数据桥导出共用）

| 助手 | VA | 语义 |
|---|---|---|
| eval_wrapper | 0x10030220 | `VALUE (const char* expr, int* err)` — rb_eval_string_protect 风格；err==0 表示成功 |
| rb_type | 0x10004740 | Ruby 1.9 类型号：4=Float 5=String 10=Bignum 12=T_DATA 20=Symbol 21=Fixnum |
| NUM2INT(fix/big) | 0x10004710 | Fixnum/Bignum → int |
| rb_num2dbl | 0x10034BA0 | 数值 → double |
| RSTRING 指针 | 0x10037A70 | 取 String 的 char* |
| rb_str_new_cstr | 0x10036340 / rb_str_new(宽字节版) 0x10036290 | 建串 |
| rb_id2str | 0x10048270 | Symbol 的 id → String（配合 `v>>8`，1.9 符号值 = id<<8\|0x0e） |
| rb_enc_ 默认编码/关联 | 0x10054500 / 0x100556A0 | SetString 族使用 |
| rb_gc_start | 0x10058CC0 | RGSSGC 直接转调 |

## 1. 生命周期 / 主循环

| 导出 | VA | 签名与语义（反编译还原） |
|------|----|-----------|
| RGSSInitialize3 | 100035C0 | `HINSTANCE (HINSTANCE)` — 先引擎初始化 sub_1000CCA0()（见 §6），hInst 存全局 |
| RGSSFinalize | 100035E0 | `void()` — 清运行标志 dword_1026301C |
| RGSSGameMain | 100036E0 | `void __noreturn (int a, const WCHAR* scriptsPath, int c)` — `new CGameFrame`(0x26C 字节, 构造 0x10001000) 存单例 0x1025EB00；`ret = frame->Run(a, path, c)`（0x10001620）；**`exit(ret)`**（3 代进程在此退出，Game.exe 不再收控） |
| RGSSGC | 10004700 | thunk → rb_gc_start（0x10058CC0） |
| RGSSErrorType | 100040B0 | `WCHAR*()` — 填充并返回 0x10260310（**宽字符**缓冲 1024）via sub_1000D330 |
| RGSSErrorMessage | 100040D0 | `WCHAR*()` — 填充并返回 0x10260B10（宽字符 1024）via sub_1000D370(buf,1024,&flag) |

## 2. 求值与数据桥

| 导出 | VA | 语义 |
|------|----|------|
| RGSSEval | 10003740 | `int (const char* expr)` — eval_wrapper；返回 *err* |
| RGSSGetBool | 10003770 | `BOOL (expr)` — **result == 2 (Qtrue) 严格判真**；nil/false/其它真值(如 1、"") 均 FALSE（1.9 常量：Qfalse=0,Qnil=4,Qtrue=2） |
| RGSSGetInt | 100037A0 | type∈{21 Fixnum,10 Bignum} → NUM2INT；type==4(Float) → (int)num2dbl；否则 0 |
| RGSSGetDouble | 10003820 | type∈{21,10,4} → num2dbl；否则 0.0 |
| RGSSGetTable | 10003890 | `void (DWORD out[4], expr)` — Check_Type(v,12=T_DATA)；data=*(v+16)；out[0..3]=data[3],[4],[5],[7]（xsize/ysize/zsize/容量，202E 同构） |
| RGSSGetSymbol | 10003930 | type==20 → id2str(v>>8) → char*；否则默认空串 0x1025A038 |
| RGSSGetStringUTF8 | 10003990 | type==5 → RSTRING_PTR；否则默认串 0x1025A03C |
| RGSSGetStringACP | 100039E0 | UTF8 → (MultiByteToWideChar **0xFDE9**=CP_UTF8) → WideCharToMultiByte(CP_ACP=0)；空串走默认 0x1025A040 |
| RGSSGetStringUTF16 | 10003B50 | UTF8 → UTF-16（0xFDE9）；rb_str_new(宽字节) 包回字符串返回指针；默认 0x1025A044 |
| RGSSSetString | 10003C70 | `int (const char* expr, const void* data, size_t len)` — strncpy(全局 0x1025F310 区, expr, 0x3E8)+NUL 填充 → eval → 校验 T_STRING → rb_enc_associate(默认编码) → rb_str 替换内容(0x10038900, ptr,len) |
| RGSSSetStringUTF8 | 10003D20 | 同上，内容为 C 字符串（0x100389C0）；缓冲 0x1025F710 |
| RGSSSetStringACP | 10003DD0 | ACP →(MB2WC flags=1)→ UTF8(0xFDE9) 再写入；缓冲 0x1025FB10 |
| RGSSSetStringUTF16 | 10003F70 | UTF16 →(WC2MB 0xFDE9)→ UTF8 写入；缓冲 0x1025FF10 |

SetString 族统一模式：专用全局表达式缓冲（0x3E8 上限）+ dword_101A2648（4 字节 NUL 填充）→ eval → T_STRING 校验 → 编码关联 → 内容替换；返回 1/0。

## 3. RTP / 路径 / 字体

| 导出 | VA | 语义 |
|------|----|------|
| RGSSSetupRTP | 10004230 | `int (const WCHAR* iniPath, WCHAR* outName, size_t size)` — 先 ClearRTPPath；RegOpenKeyExW(HKLM, "Software\...\RTP", KEY_READ)；i=1..9："RTP"/"RTP%d" → GetPrivateProfileStringW([Game]节) → RegQueryValueExW(值名) → 去尾 '\\' → **AddRTPPath + SetupFonts(每条)**；未命中且 outName≠NULL → 拷名字返回 0；否则 1 |
| RGSSAddRTPPath | 100046B0 | thunk → sub_1000D7D0(登记搜索路径) |
| RGSSClearRTPPath | 100046D0 | thunk → sub_1000D830 |
| RGSSGetRTPPath | 10004690 | thunk → sub_1000D840 |
| RGSSGetPathWithRTP | 10004100 | `WCHAR* (const WCHAR* name)` — len∈(0,260) → 转多字节 → sub_1000DAB0 在 RTP 路径集解析 → 全局 0x10261310（宽字符结果）；非法输入返回默认串 |
| RGSSSetupFonts | 100046E0 | thunk → sub_1000D870(固定目录串 String2)（".\Fonts" 注册，2 代同构） |

## 4. 音频

| 导出 | VA | 语义 |
|------|----|------|
| RGSSAudioInitialize | 100035F0 | `int (HWND)` — 若单例 0x1025EB04 已存在先虚析构；new 0xA4 字节（ctor 0x10109DA0，CSxAudio 系）存单例；返回 sub_1010A360(hwnd)（后端初始化） |
| RGSSAudioFinalize | 10003690 | 虚析构单例（vtable[0](1)）并清零 |

## 5. RGSS3 特有行为（对照 202E 差异）

- 导出面从 69 缩到 27：**regex_*/ruby_re_*/Reg* 整族消失**（1.8 的正则导出为 2 代 Game.exe 服务；3 代 Game.exe 不再用）。
- RGSSInitialize2(hwnd) → **RGSSInitialize3(hInstance)**；RGSSErrorType/Message 缓冲从窄字符 1024B → **宽字符 1024**。
- RGSSGameMain 不再返回：**exit(ret)**。
- RGSSGetBool 从 "≠Qfalse 判真"（2 代）变为 **==Qtrue 严格判真**。

## 6. 核心内部函数（M2 附带还原）

- **引擎初始化 sub_1000CCA0**（RGSSInitialize3 调用）：
  ```
  ruby_sysinit(0x102AB474/78) → ruby_init(0x1006AF00)
  → 编码默认设置(0x1000C580, aEncodingDefaul)
  → rb_define_global_func("load_data", 0x1000CD60, 1)      // RGSS 内建
  → rb_define_global_func("save_data", 0x1000CE90, 2)      // RGSS 内建
  → rb_define_const("RGSS_VERSION", "3.00")                 // aRgssVersion/a300
  → Init×5: 0x10004830 / 0x10013E40 / 0x100086A0 / 0x10015BB0 / 0x10011510 / 0x100047C0
  → GetUserDefaultUILanguage()&0x3FF==17(日语) ? word_10263020=100 : 0
  ```
  `word_10263020` = **字符串表语言偏移**（0=英/汉资源组，100=日文组；串号=base+偏移，
  解释 M1 报告中 `word+0xCF(207)` 之谜：207="Failed to initialize DirectX Audio."）。
- **CGameFrame::Run sub_10001620(this, hwnd, scriptsPath, flags)**（主流程，节选）：
  WndProc 子类化(GWL_WNDPROC→0x10001C80) → 图形屏初始化(vcall this+580) →
  sub_1010DD70(**544,416**,0)（RGSS3 内部基准分辨率！） → 加速键表(0x65) →
  环境检查链（OS≤Win9x→串202 / 系统目录关键文件缺失→203 / CPU 不兼容(0x1010F5A0)→204 /
  色深<16bpp→205 / 音频初始化失败 sub_1010A360→206 / sub_10019B70 失败→207 弹框）→
  SetThreadExecutionState(3) → 双视口建立（this+592 z=0x7FFFFFFE / this+600 z=0x7FFFFFFF，
  尺寸 640×480） → 系统菜单加"属性(&P)\tF1"（InsertMenuW id **0x7D1**，串=word+213）→
  注册表("Software\Enterbrain\…") → 测试模式启动画面（隐藏光标 + timeGetTime 补足 1000ms）→
  **进入真主循环 sub_1000E9E0(scriptsPath, flags)**。
- **rb_raise 实现 sub_10026210(klass, fmt, ...)**：vsprintf → rb_str_new →
  `rb_funcall(klass, "new", 1, msg)` → rb_exc_raise。F12 轮询函数 0x1001EB0 即
  `GetKeyState(VK_F12)<0 → rb_raise(*(0x10261BC4 /*RGSSReset 类*/), 0x101A25EE)`。
- sub_10019B70：音频后端就绪/状态检查（Run 在音频初始化后调它判失败，返回 bool；
  内部为 std::string 运算族 0x10019D50/0x1001A9F0 + 比较 0x10013D00——精确语义挂 M5）。

## 附：apphelp 污染槽定名（M1 已结案，M2 复核一致）

| IAT 槽 | 定名 | 证据 |
|--------|------|------|
| 0x101A236C | user32!IsWindowVisible | 单参；neg/sbb/and 0x10000000(WS_VISIBLE)；=202E 蹦床 7014DEA0 |
| 0x101A2390 | user32!MessageBoxW | 4 参(hwnd,text,caption,uType)；全局消息缓冲 0x10263068；=202E 蹦床 701500B0 |
