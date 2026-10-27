# M2 — Game.exe ↔ RGSS202E.dll 接口文档
> 注：本文写于 2026-10-27 仓库重构前；文中 `backup/` `unpacked/` `build/` `test/` `third_party/` 等路径现位于 `local/` 下，`src/rgss_*.c` 现为 `src/runtimes/rgss2/*.c`，`tools/` 已按流水线分桶（unpack/pe/analysis/probe/verify）。


> 依据：69 个导出（exports.txt）+ Hex-Rays 反编译（24/33 成功，其余手工/汇编级还原）。
> 地址均为 VA（镜像基址 0x10000000）。

## 调用契约（Game.exe 侧典型流程，推断自导出语义）

```
hWnd 已由 Game.exe 创建 → RGSSInitialize2(hWnd)
                           RGSSSetupFonts()
                           RGSSSetupRTP(rtpPaths, iniRtp, size)   // RTP= 为空则只扫注册表
                           RGSSAudioInitialize()
                           ret = RGSSGameMain(a, b, c)            // 主循环, 返回退出码
                           RGSSAudioFinalize()
                           RGSSFinalize()
```

## 1. 生命周期 / 主循环

| 导出 | VA | 签名与语义 |
|------|----|-----------|
| RGSSInitialize2 | 100027F0 | `int RGSSInitialize2(int hwnd)` — 先 sub_10009E60()（引擎初始化），hwnd 存全局 0x1017347C |
| RGSSFinalize | 10002800 | `void()` — 清 0x10173478（运行标志） |
| RGSSGameMain | 10002890 | `int (int a, int b, int c)` — 主循环（Hex-Rays 噎点，已手工还原）：`$TEST` 环境变量==2 设测试标志(0x1016EFE0)；`new CGameFrame`(0x26C 字节, 构造 0x10001020)；单例存 0x1016EFE4；`ret = frame->Run(a,b,c)`（0x10001F00）；虚表[0](1) 析构；返回 ret |
| RGSSGC | 10002D60 | thunk → GC 触发 |
| RGSSErrorType | 100029C0 | `int*()` — 返回错误类型缓冲 0x1016F7F8（1024B） |
| RGSSErrorMessage | 100029E0 | `int*()` — 返回错误消息缓冲 0x1016FFF8（1024B） |

## 2. 求值与数据桥（核心原语：`eval_wrapper` = sub_100330A0(str, int* err) → VALUE）

所有 Get*/Set* 都走 sub_100330A0（rb_eval_string_protect 风格），之后按 Ruby 1.8 VALUE 语义分派：
`(v&1)!=0` Fixnum；`v==2/4/6/14` 特殊常量（nil/false/true/symbol）；`(RBasic->flags & 0x3F)==7` T_STRING；`==0xA` T_FLOAT（RGSSGetInt/Double 的类型判断组合）。

| 导出 | VA | 语义 |
|------|----|------|
| RGSSEval | 10002910 | `int (const char* code)` — eval，返回错误码 |
| RGSSGetBool | 10002930 | eval 结果==Qfalse(2) 判假 |
| RGSSGetInt | 10002D70 | Fixnum→v>>1；Float→rb_num2int；异常路径处理 |
| RGSSGetDouble | 10002E00 | Float/int → double |
| RGSSGetTable | 10002960 | `void (Table* out, const char* expr)` — T_DATA 直接读内嵌结构：out[0..3] = tbl[3],[4],[5],[7]（xsize/ysize/zsize/…） |
| RGSSGetStringUTF8 | 10002E70 | T_STRING → RSTRING_PTR（sub_100808D0） |
| RGSSGetStringACP | 10002ED0 | UTF8 → CP_ACP（MultiByteToWideChar(65001→0)+WideCharToMultiByte） |
| RGSSGetStringUTF16 | 10002FF0 | UTF8 → UTF-16（代码页 65001），返回宽串 |
| RGSSSetString / UTF8 / ACP / UTF16 | 100030E0 / 10003180 / 10003220 / 10003350 | 统一模式：sprintf(全局缓冲@0x1017?0F8 区, fmt, …,1000) + NUL + eval + T_STRING 校验 + rb_str 替换内容（sub_10080C10/CA0）；ACP/UTF16 版先转 CP_UTF8 再 eval |

## 3. RTP / 路径 / 字体

| 导出 | VA | 语义 |
|------|----|------|
| RGSSSetupRTP | 10002AA0 | `(const wchar* gameDir?, const wchar* iniRtp, int len)` — RegOpenKeyExW(HKLM, "Software\\Enterbrain\\RGSS2…")（off_100FF008），遍历 RTP/RTP1..RTP9 值（SHGetValueW=off_100FF00C），去尾部 '\\'，RGSSAddRTPPath 逐个登记 |
| RGSSAddRTPPath | 10002D30 | thunk → 添加搜索路径 |
| RGSSClearRTPPath | 10002D40 | thunk → 清空 |
| RGSSGetRTPPath | 10002D20 | thunk → 取路径串 |
| RGSSGetPathWithRTP | 10002A10 | `(const wchar* name) → wchar*` — 在 RTP 路径中解析 name（<260），命中则返回全路径，否则返回默认串 |
| RGSSSetupFonts | 10002D50 | → sub_10009110(".\\Fonts"?)（asc_100FF63C）字体目录注册 |

## 4. 音频

| 导出 | VA | 语义 |
|------|----|------|
| RGSSAudioInitialize | 10002810 | CSxAudio 初始化（Hex-Rays 噎点；汇编：创建 0x164 字节 CSxAudio 单例→0x1016EFE8） |
| RGSSAudioFinalize | 10002870 | 虚析构 0x1016EFE8 单例并清零 |

## 5. Ruby 正则引擎透传（36 个导出 = Ruby 1.8.1 re.c/regex.c 公开符号）

`regex_new / regex_free / regex_search / regex_match / regex_init / regex_end / regex_region_* /
ruby_re_* / regex_set_warn_func / enc_get_* / regex_version…` —— 与 Ruby 1.8.1 源码
`re.c`/`regex.c` 一一对应（M3 集成官方源码后直接复用，无需逆向）。

## 6. 编码/语法注册（数据型导出）

`RegDefaultSyntax(0x15C030) RegSyntaxRuby(0x15C020) RegEncodingASCII(0x15C4B8)
RegEncodingUTF8(0x15DDB8) RegEncodingEUC_JP(0x15DF00) RegEncodingSJIS(0x15E048)
EncDefaultCharEncoding(0x15C5FC)` —— 指向 Ruby 正则全局配置对象，供宿主查询。

## 附：apphelp 污染槽定名（M2 结案）

| IAT 槽 | 定名 | 证据 |
|--------|------|------|
| 0x100FF2DC | **user32!MessageBoxW** | sub_100015A0: `(this[2]/*hwnd*/, text, this+3/*caption*/, 0x30)`；**0x30=MB_ICONWARNING**；弹框前 sub_10097630/sub_10097660 暂停/恢复输入；sub_100BC650 拼接 "CnxfileObject" 错误消息后弹框 |
| 0x100FF324 | **user32!IsWindowVisible**（85%，备选 IsWindow） | 单参 hwnd；返回值直接决定 `SetWindowLongW(hwnd, GWL_STYLE=-16, … \| WS_VISIBLE(0x10000000))` |
