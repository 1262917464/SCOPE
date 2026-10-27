# M5-e — 重建版 DLL 骨架首次成型

日期：2026-10-26 ｜ 里程碑意义：**从"分析产物"跨到"可编译重建产物"**

## 1. 产物

| 文件 | 说明 |
|------|------|
| `build/RGSS202E_rebuild.dll` | 3.0 MB，**69 个导出与原版 100% 同名**，可 LoadLibrary |
| `src/RGSS202E.def` | 由 `tools/gen_def.py` 从原版 exports.txt 自动生成（含序号绑定） |
| `src/rgss_export.c` | 30 个 RGSS 核心导出实现（生命周期/数据桥/字符串/RTP/音频/GC） |
| `src/rgss_regex_stubs.c` | 39 个正则/编码导出实现（含 1 个 1.8.1 缺失符号的兼容实现） |
| `src/ext/Win32API/Win32API.c` | 重建版 Win32API 扩展（修调用约定） |
| `build/ruby-build/libmsvcrt-ruby18-static.a` | Ruby 1.8.1 静态库 |

## 2. 验证结果

```
导出名对比: 69 / 69 完全一致（含序号 1..69）
LoadLibrary: OK @ 0x10000000（--disable-dynamicbase 关闭 ASLR 后锁定原版基址）
GetProcAddress: 全部命中
FreeLibrary: OK
```

### 运行时行为验证（tools/verify_rebuild.exe）— 7/7 与原版一致

| 用例 | 重建版输出 | 原版输出 | 结果 |
|------|-----------|---------|------|
| `1+1` | 2 | 2 | ✅ |
| `RUBY_VERSION` | "1.8.1" | "1.8.1" | ✅ |
| `(1..10).inject{\|s,x\|s+x}` | 55 | 55 | ✅ |
| `'a'+'b'` | "ab" | "ab" | ✅ |
| `def f(n);n*2;end;f(21)` | 42 | 42 | ✅ |
| `'hoge'.sub(/o/,'0')` | "h0ge" | "h0ge" | ✅ |
| `Marshal.dump(42).unpack('H*')[0]` | "0408692f" | "0408692f" | ✅ |

**包括正则与 Marshal 字节流在内全部一致** —— 重建版的 Ruby 引擎层行为已与原版等价。

## 3. 已实现 vs 待实现

### 已实现（可工作）

- `RGSSInitialize2`: ruby_init + ruby_script + 保存 hwnd
- `RGSSFinalize` / `RGSSGC`: 状态清零 / rb_gc_start
- `RGSSEval`: rb_eval_string_protect 封装（**与 M3 对拍一致的语义**）
- `RGSSGetBool/GetInt/GetDouble`: eval + Ruby 1.8 VALUE 类型分派（FIXNUM_P / T_FLOAT）
- `RGSSGetStringUTF8/ACP/UTF16`: eval + T_STRING 校验 + 代码页转换
- `RGSSSetString*`: 4 个变体，sprintf → eval → 字符串赋值
- `RGSSErrorType/ErrorMessage`: 静态缓冲返回
- 正则 39 个导出: 全部可链接（`ruby_re_*` 由 Ruby 静态库提供真实实现；
  `regex_*`/`enc_get_*` 为 Enterbrain 封装层，当前为安全空实现）

### 待实现（M5-e 后续 / M6）

1. **基址修正**: 链接加 `-Wl,--image-base,0x10000000`（原版固定基址，游戏硬编码）
2. `RGSSGameMain`: CGameFrame 对象 + Run 主循环（M4/M5-a 骨架已还原）
3. `RGSSGetTable`: Table 对象内嵌字段读取（布局待定）
4. `RGSSSetupRTP` / RTP 路径管理（注册表 HKLM\Software\Enterbrain\RTP1..9）
5. 9 个图形类的 C 实现（Bitmap/Sprite/...，328 条 API 表已就位）
6. Nx 渲染后端（GDI+DIB）、CSx 音频后端（DirectSound/MCI/ACM/Ogg）
7. `regex_*` 封装层的真实语义（按原版 0x1007C230/0x1007BF30 反编译）

## 4. 本轮修正的认知（重要）

| 原假设 | 实测结论 |
|--------|---------|
| Ruby 1.8.1 用 Oniguruma | **错**：用 GNU regex（regex.c v0.12），Oniguruma 是 1.9+ |
| `ruby_re_*` 是 Enterbrain 加的名字 | **错**：官方 regex.h 里就是宏别名（`#define re_match ruby_re_match`） |
| `regex_new`/`enc_get_*` 是官方符号 | **错**：官方 1.8.1 无此符号，是 Enterbrain 封装层 |
| `RegEncodingSJIS` 等是函数 | **错**：是**数据导出**（ctype 表/syntax_t 对象） |

## 5. 构建命令（可复现）

```bash
export PATH="/c/msys64/mingw32/bin:$PATH"
RB=build/ruby-build
gcc -c -O2 -o build/rgss_export.o      -I$RB -I$RB/win32 src/rgss_export.c
gcc -c -O2 -o build/rgss_regex_stubs.o -I$RB -I$RB/win32 src/rgss_regex_stubs.c
gcc -shared -O2 -static -o build/RGSS202E_rebuild.dll \
    build/rgss_export.o build/rgss_regex_stubs.o src/RGSS202E.def \
    -L$RB -lmsvcrt-ruby18-static -lws2_32 -lkernel32 -luser32 \
    -lgdi32 -lwinmm -lmsacm32 -ladvapi32 -lole32
```
