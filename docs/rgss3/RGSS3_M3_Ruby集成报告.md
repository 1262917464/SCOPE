# RGSS3 M3 报告 — Ruby 1.9.2p0 集成与内建脚本取证

日期：2026-10-26 ｜ 前置：M1（基线）、M2（接口/模块地图）

## 0. 一句话结论

**Ruby 1.9.2p0 官方源码已用 mingw32 成功编译为静态库并嵌入重建 DLL**；
`build/RGSS3_300/RGSS300.dll` 可加载、27 导出 1:1 匹配原版。
同时**从 DLL 中提取出 20,623 字节的 RGSS3 内建 Ruby 脚本**（45 个 RPG 类）
与 288 条 Ruby 侧 API 名——这是"完整逻辑"里此前未知的一整层。

## 1. Ruby 静态库构建（M3.1）

| 项 | 值 |
|---|---|
| 源码 | ruby-1.9.2-p0.tar.gz（SHA-256 8c0c4e26…285c），third_party/ |
| 工具链 | MSYS2 mingw32 gcc 12.2.0、make 4.4 |
| 配置 | `./configure --host=i386-mingw32 --build=i386-mingw32 --disable-shared --with-static-linked-ext` |
| 产物 | `build/rubylib/libruby192-static.a`（10,707,872 B）+ 头文件；`miniruby.exe` 8.4MB |
| 验证 | `miniruby -v` → **`ruby 1.9.2p0 (2010-08-18) [i386-mingw32]`**（与 DLL 内嵌版本串完全一致） |
| 嵌入验证 | 测试程序 `ruby_sysinit→ruby_init→rb_eval_string_protect` 输出 `1.9.2 / i386-mingw32 / 3` ✅ |

### 构建过程中解决的环境/源码适配问题（可复现清单）

| # | 症状 | 根因 | 处置 |
|---|------|------|------|
| 1 | `invalid variable name: '0'` / `.cvsignore` expr 报错 | PATH 里 SourceTree 自带的**老 sed** 抢先于 msys2 GNU sed 4.9 | 用**干净 PATH**（只含 mingw32/usr/bin/Windows）重跑 configure |
| 2 | `C compiler cannot create executables` | autoconf 2.65 把 `backtrace` 链接失败当致命 | 预置 `ac_cv_func_backtrace=no ac_cv_lib_execinfo_backtrace=no` |
| 3 | config.h 残缺（无 SIZEOF_*/rb_pid_t） | 同上 sed 污染的连带后果 | 干净 PATH 后 config.h 完整（175 行/174 define） |
| 4 | make 反复 `exec autoconf: not found` | 未装 autoconf 且 configure.in 时间戳更新 | 造 no-op `autoconf` shim 于 `build/fakebin/`；并钉住时间戳 |
| 5 | `executable host ruby is required` | 需 BASERUBY 生成 parse.c/insns.inc/prelude.c 等 | **先编出 miniruby**，再 `make BASERUBY=./miniruby.exe` |
| 6 | `rb_pid_t` 未定义 | 上游 1.9.2 mingw 坑 | 干净 configure 后自动正确（`#define rb_pid_t pid_t`） |
| 7 | truncate/ftruncate/fseeko64/ftello64 类型冲突 | `win32.h` 把 `off_t` 硬定义为 `__int64`，与应用 mingw 头冲突 | `include/ruby/win32.h` 加 `__MINGW32__` 保护（3 处） |
| 8 | `computed goto must be pointer type` | Ruby VM 直接线程化代码与 mingw gcc12 不兼容 | `vm_opts.h`: `OPT_DIRECT_THREADED_CODE 1→0`（走 switch 分发，语义等价） |
| 9 | `-mno-cygwin unrecognized` | 现代 gcc 已移除该选项，GNUmakefile 无条件追加 | `GNUmakefile:11` 删除该行 |
| 10 | `undefined reference to Init_prelude` | prelude.c 被误 touch 成空文件 | `make prelude.c BASERUBY=./miniruby.exe` 重新生成（15,651 B） |
| 11 | name2ctype.h 生成失败 | 规则内 `$(?:.kwd=.src)` 展开异常 | 直接从 `name2ctype.h.blt` 复制（预生成件） |

> 复现：`build/fakebin/autoconf`（no-op）+ 干净 PATH + `make BASERUBY=./miniruby.exe`。
> 全部适配都是**构建环境层**的，未改动 Ruby 语言语义。

## 2. 内建脚本取证（M3.2）— 本轮最重要的发现

### 2.1 RGSS3 内建 Ruby 脚本（20,623 字节）

- 位置：**RVA 0x1A26B0**（.rdata），由指针表槽 0x25A048 引用。
- 内容：`module RPG;end;class RPG::Map; …` 连续 20,623 字节 Ruby 源码，
  **45 个 RPG 数据类**：RPG::Map(/Encounter) → MapInfo → Event(/Page/Condition/Graphic)
  → EventCommand → MoveRoute/MoveCommand → BaseItem → Actor → Class → … →
  System(/Vehicle/Terms/TestBattler) → AudioFile → BGM/BGS/ME/SE。
- 已存：`unpacked/RGSS3_300_pristine/embedded/rgss3_internal_script.rb`
- 引用 14 个 RGSS 内建 API：`Audio.{bgm,bgs,me,se}_{play,stop,fade,pos}` + `wait`。
- **重建要求**：这份脚本必须逐字节复刻（它是运行期 eval 的资产，不是 C 代码）。

### 2.2 Ruby 侧 API 名全表（288 条）

- 范围 RVA 0x1A7740–0x1A8B00，已存 `embedded/rgss3_api_names.txt`。
- 覆盖类：`Audio`（13 方法 + setup_midi + __reset__）、`Bitmap`（blt/stretch_blt/
  fill_rect/clear/gradient_fill_rect/get_pixel/set_pixel/hue_change/blur/radial_blur/
  draw_text/text_size/dispose/disposed?/rect/width/height）、`Font`（name/size/bold/
  italic/outline/shadow/color/out_color）、`Color`/`Tone`/`Rect`（reader+writer 双份）、
  `Table`（resize/[]/[]=/xsize/ysize/zsize）、`Sprite`（src_rect/z/x/y/ox/oy/zoom/angle/
  mirror/wave_*/bush_*/flash/bitmap/viewport）、`Viewport`（rect/visible/z/ox/oy/color/
  tone/dispose）、`Tilemap`（map_data/flags/bitmaps/…）、`Window`（windowskin/contents/
  cursor_rect/active/pause/arrows_visible/padding/back_opacity/contents_opacity/openness/
  close?/open?/move + …）。
- **这张表 = M4 的实现清单**（每个名字对应一个 rb_define_method 注册）。

### 2.3 字符串双向差集（补丁取证）

- 官方 Ruby 源码（398 个 C/H 文件）抽取 ASCII 字面量：**8,055 条**（≥6 字节）。
- DLL 内 ASCII 串（≥6 字节）：**8,625 条**，其中 **2,619 条能与官方源码对上**。
  ⇒ 强证据：RGSS300.dll 确实由官方 1.9.2p0 源码编译而成（非重写）。
- 官方有/DLL 无（≥12 字节）共 **3,345 条**，主体是：
  - 未编入的扩展（tk/tcl、win32ole、openssl 等在 `ext/` 下未参与构建）；
  - `bcc32/`、测试与 benchmark 相关串；
  - 部分 `-W` 选项帮助文本（1.9.3 特性，1.9.2p0 源码里也有但未进 DLL）。
  ⇒ 说明 Enterbrain 用的是**裁剪过的最小构建**（这也是 M3 只静态链接核心的原因）。
- 结论：**未发现 Enterbrain 对 Ruby 核心做语义性改写的迹象**；
  其定制集中在（a）RGSS 模块注册、（b）内建脚本、（c）引擎层，而非解释器本体。

## 3. 重建 DLL（M3.3）

| 项 | 结果 |
|---|---|
| 源码 | `src/RGSS3_300/`：`RGSS300.h`（27 导出原型，取自 M2 反编译）、`rgss_core.c`、`RGSS300.def`（序号 1–27 与原版一致）、`Makefile` |
| 产物 | `build/RGSS3_300/RGSS300.dll`（8,403,835 B，PE32 i386） |
| 导出 | **27/27，名与序号与原版逐一匹配** |
| 加载 | LoadLibrary 成功（基址 6b550000 动态分配），FreeLibrary 干净 |
| 已实现语义 | RGSSInitialize3（sysinit/ruby_init/编码/load_data/save_data/RGSS_VERSION/UI 语言判定）、RGSSEval/GetBool/GetInt/GetDouble/GetTable/GetSymbol/GetString*/SetString*（严格按 M2 的 1.9 类型码与 Qtrue 判真）、RGSSErrorType/Message（宽字符 1024）、RTP 全套（HKLM RGBS3\RTP × RTP..RTP9 + ini 读取 + 去尾反斜杠 + 字体注册）、RGSSGC、音频骨架 |
| 占位 | RGSSGameMain（主循环 M4）、RGSSAudioInitialize/Finalize（M5 CSx 镜像） |

注：DLL 体积 8.4MB 是因为静态链接了整个 Ruby 解释器（原版 1.19MB 带壳/3.3MB 脱壳，
其 root 结构相同，M6 可做裁剪与 -Os 优化）。

## 4. M4 入口清单（已由 M3 结果细化）

1. 按 `rgss3_api_names.txt` 逐类注册：Bitmap/Font/Color/Tone/Rect/Table/Sprite/
   Viewport/Tilemap/Window/Audio + RGSS 内部模块（Graphics/Input/RGSS/等等）。
2. **内嵌脚本必须逐字节复刻**：M4 启动时 eval `rgss3_internal_script.rb`；
   脚本依赖的 14 个 Audio API 由 C 侧提供。
3. CRx* 类（vftable 已在 M2 §2 定位）→ 对应上述 Ruby 类的数据承载对象。
4. M5：NxDrawLocal（GDI/DIB，含 8/32bpp 双实现）替换占位渲染；
   CSx* + Theora 替换音频骨架（`CSxDirectSound` 经 combase!CoCreateInstance 创建）。

## 5. 产物清单

```
build/rubylib/libruby192-static.a       Ruby 1.9.2p0 静态库（10.7MB）
build/rubylib/include|extinclude/        配套头文件（config.h 完整）
build/ruby-build-192/miniruby.exe        可作 BASERUBY 的解释器（8.4MB）
build/fakebin/autoconf                   no-op shim（构建基础设施）
build/RGSS3_300/RGSS300.dll              ★ 重建 DLL（27 导出 1:1）
src/RGSS3_300/{RGSS300.h,RGSS300.def,rgss_core.c,Makefile}
unpacked/RGSS3_300_pristine/embedded/
  ├── rgss3_internal_script.rb           ★ 内建 Ruby 脚本 20,623 B / 45 类
  └── rgss3_api_names.txt                ★ 288 条 Ruby 侧 API 名（M4 清单）
build/rgss3_strdiff.txt                  字符串双向差集取证
tools/rgss3_strdiff.py                   取证脚本
```
