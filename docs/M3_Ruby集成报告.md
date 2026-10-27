# M3 报告 — Ruby 1.8.1 官方源码 mingw32 集成

日期：2026-10-26 ｜ 验收标准：重建侧 RGSSEval("1+1")==2 等价跑通 → **达成（10/10 + 原版对拍 5/5）**

## 1. 构建

- 源码：third_party/ruby-1.8.1（官方 tarball，SHA-256 d0f1e19f…de4e）
- 工具链：C:\msys64 mingw32（i686-w64-mingw32-gcc 12.2.0）+ msys2 make（本机缺，已 pacman 安装）
- configure 一次通过：`sh configure --host=i686-w64-mingw32 --build=i686-w64-mingw32 --disable-shared --enable-static`
- 源码补丁仅 3 处（全部集中在 tools/ruby_mingw_patches.py，幂等可复现）：
  1. `win32.h` gettimeofday 原型 `struct timezone*` → `void*`（与 mingw-w64 sys/time.h 对齐，ABI 兼容）
  2. `win32.h` struct timezone 定义加 `_TIMEZONE_DEFINED` 双向守卫（与 mingw 头去重）
  3. `win32.c` gettimeofday 定义同步改 `void*`（经 #define 还原内部名）
- 时间戳坑（复现必读）：tarball 复制后 make 想重跑 autoconf/yacc → 按 `aclocal.m4 → configure → config.status → Makefile`、
  `parse.y → parse.c` 顺序 touch。
- 产物：`build/ruby-build/libmsvcrt-ruby18-static.a`（41 对象，725 导出符号）。
  注意 make -j 中断过一次会留下 **0 成员空壳 .a** 且时间戳较新，重跑前必须删掉。
- ext 层（win32ole 等）未编：RGSS 不需要；Win32API 扩展按需在 M4 补。

## 2. 验收结果

### 重建侧电池（src/m3test/host.c + ruby_test.dll）— 10/10 PASS

`1+1=2`、`RUBY_VERSION=="1.8.1"`、字符串拼接、inject 块、map/join、
`Marshal.dump(42).unpack('H*')[0]=="0408692f"`、方法定义、正则 sub、nil.inspect、
raise 异常消息 `RuntimeError: (eval)boom`、`RubyEval("6*7")==42`。

### 原版对拍（src/m3test/oem_eval.c，首次实弹对照）— 5/5 一致

原版带壳 `backup/RGSS202E.dll` 在裸进程（无窗口）中：
`RGSSInitialize2(0)=0` 成功，`RGSSEval/RGSSGetInt/RGSSGetStringUTF8` 全部可用。
与重建 Ruby 输出**逐项一致**（含 Marshal 字节流 `0408692f`）。

**重大红利**：原版 DLL 可被外部 harness 直接驱动跑任意 Ruby 代码
→ M6 对照测试的"原版侧"通道已打通，无需跑完整游戏即可对拍解释器行为。

## 3. M2 接口文档的交叉验证

oem_eval 实测证实：RGSSInitialize2(hwnd 可为 0)、RGSSGetInt=eval+类型分派、
RGSSGetStringUTF8=eval+RSTRING_PTR，与 M2 反编译结论一致。

## 4. 遗留（转 M4/M6）

1. Enterbrain 对 Ruby 1.8.1 的本地补丁：目前 5 条对拍全部一致（含较深路径 Marshal/块/异常）；
   系统性差异用 M6 全量 Ruby 电池实证（比静态 diff 更硬），异常点再回 IDA 定位。
2. Win32API 扩展（RGSS 带此扩展）：M4 按需编译 ext/Win32API。
3. Ruby GC 与 RGSS 生命周期（RGSSGC 导出→rb_gc_start 映射）：M4。
