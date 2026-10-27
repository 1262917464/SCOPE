# RESTRUCTURE_PLAN — SCOPE 仓库结构重构工单

> 状态：**待执行**。执行完在文首标注"已完成（日期）"，然后整体归档到 `docs/adr/0001-repository-restructure.md`。
> 产出方式：2026-10-27 与主人逐轮问答敲定（Q1–Q31，决策记录见 §A）。
> 本文是唯一工单；执行会话不得另行发明结构。执行中遇到本文未覆盖的情况：停下来，报告，等主人裁决。

---

## 0. 一句话

把"RGSS202E 单 DLL 逆向"的树，改成"SCOPE 计划、四个运行时并列、工作台收进 local/、文档按文体分家"的树。**四个运行时彼此独立**（仅工具链与逆向思路可互参），仓库只提交"源码 + 文档"，一切可再生成物与著作权物进 `local/`。

## 1. 目标树（重构后）

```
<repo>\                              # 目录名保持 E:\rgss2-rebuild 不变（Q6）
├── RESTRUCTURE_PLAN.md              # 本文件（执行完归档，见 §0）
├── README.md                        # 重写：SCOPE 计划总览（§7）
├── LICENSE                          # 新增：All Rights Reserved + 边界声明（§7）
├── .gitignore                       # 重写为极简版（§7）
├── .gitattributes                   # 保留，追加 *.rb 已有、无改动
├── docs\
│   ├── reports\                     # 历史叙事（按 运行时\ 编号文件名保留）
│   │   ├── rgss2\M1_脱壳与基线报告.md … M5-k_字体创建逆向修正.md   # 17 份，见 §4
│   │   ├── rgss1\M1_脱壳与基线报告.md … （docs\rgss1\ 现有 3 份报告）
│   │   ├── rgss3\RGSS3_M1_基线与修改鉴定报告.md … （docs\rgss3\ 现有 4 份报告）
│   │   └── rm2k\M2_主循环与场景状态机.md … （docs\rm2k\ 现有 2 份报告）
│   ├── analysis\                    # 可长期引用的逆向事实
│   │   ├── rgss2\
│   │   │   ├── M2_Game.exe接口文档.md      # 纯 spec（69 导出契约）
│   │   │   ├── M2_模块地图.md
│   │   │   ├── M5-c_Bitmap布局与像素格式.md
│   │   │   ├── M5-c_图形类注册链.md
│   │   │   ├── M5-d_Scripts解析.md
│   │   │   ├── M5-i_字体系统逆向.md
│   │   │   ├── M5-j_draw_text逆向.md
│   │   │   ├── M5_Init注册总表.md
│   │   │   ├── api_map.tsv                 # ← 原 docs\assets\rgss_ruby_api_map.tsv
│   │   │   └── pitfalls.md                 # ← PROGRESS.md §五+Ruby陷阱（拆出，见 §4）
│   │   └── common\win32api_cdecl.md        # ← PROGRESS.md 里 Win32API 调用约定陷阱单列（§4）
│   ├── audit\                       # 白盒一致性审计（会过期，与 analysis 分开）
│   │   └── A1_data_classes.md … （现有 3 份，原地）
│   ├── progress\                    # ★唯一进度区（Q26=C）
│   │   ├── rgss2.md                 # ← 根 PROGRESS.md 的看板部分重写（§4）
│   │   ├── rgss1.md                 # ← docs\rgss1\PROGRESS.md
│   │   ├── rgss3.md                 # ← docs\rgss3\README.md
│   │   └── rm2k.md                  # ← docs\rm2k\PROGRESS.md
│   ├── adr\                         # 决策记录
│   │   └── 0001-repository-restructure.md   # 本文件执行完的归档位
│   ├── assets\                      # 只剩证据截图（被 reports 引用）
│   │   ├── kt_00_title.png、kt_01_after_enter.png、reference_map_original.png
│   │   ├── screenshot_title.png、screenshot_with_window.png、window_9slice_compare.png
│   │   └── Project3_scripts\        # 不入库，维持 gitignore（现由 local\ 承接，见 §3）
│   └── 交付要求.md                   # → 移到 docs\reports\rgss2\交付要求.md（验收标准属 rgss2 线）
├── src\
│   ├── runtimes\
│   │   ├── rgss2\
│   │   │   ├── Makefile             # ★新增，唯一构建真相（§5）
│   │   │   ├── exports.def          # ← RGSS202E.def；exports.def.txt → exports.orig.txt
│   │   │   ├── export.c classes.c data_classes.c bitmap.c graphics_classes.c
│   │   │   ├── render.c gamemain.c marshal.c embedded.c regex_stubs.c image.cpp
│   │   │   ├── ext\Win32API.c       # ← src\ext\Win32API\Win32API.c（Q28=A）
│   │   │   ├── selftest\            # ← src\m3test\ 的 .c（host.c→ruby_battery.c）
│   │   │   └── embedded\rpg_classes.rb   # ← docs\assets\embedded_rpg_classes.rb（Q9/Q27）
│   │   ├── rgss3\
│   │   │   └── 300\                 # ← src\RGSS3_300\（Q15r=A：rgss3 自选版本粒度）
│   │   │       ├── Makefile（改 RUBYLIB 引用 → local\，见 §5）
│   │   │       ├── rgss_core.c  RGSS300.def  RGSS300.h
│   │   └── rm2k\                    # ← src\rm2k\ 原样（Q29=C：暂不分 2000/2003）
│   │           GameMain.c lcf.c lcf.h lcf_test.c lcf_verify.c
│   │           platform_win32.c rm2k_delphi.c rm2k_delphi.h
│   └── mk\common.mk                 # ★新增：工具链变量 + 模式规则（§5，Q13=A）
├── tools\
│   ├── Makefile                     # ★新增：全部工具的构建入口（§5）
│   ├── unpack\   unpacker.c unpacker2.c unpacker3.c unpacker.rc unpacker.manifest
│   ├── pe\       mkpe.py mkpe2.py impcheck.c gen_def.py rsrcwalk.py peinfo2k.py peinfo_xp.py iat_static.py
│   ├── analysis\ rtti_scan.py dumpat.py decode_rbdefs.py find_init_fns.py api_usage.py api_usage2.py
│   │   │         pseudo_coverage.py fix_idalib_cfg.py ruby_mingw_patches.py rgss3_strdiff.py
│   │   │         delphi_probe.py rm2k_delphi_scan.py rm2k_disasm.py lcf_syntax_probe.py
│   │   │         probe.S probe.def probelog.c input_probe.rb
│   ├── probe\    iat_probe.c keytest.c blinkprobe.c cursortest.c probe_input_consts.c
│   │   │         probe_input2.c rgss_introspect.c rgss_introspect2.c smoke.c difftest.c
│   │   │         RGSS1_*（15 个源码文件，保留 rgss1_ 前缀，Q11=A）
│   │   │         RGSS202E_probe.dll→不迁（编译产物，见 §6）
│   ├── verify\   verify_bitmap.c verify_rebuild.c bmp2png.ps1 framediff.py verinfo_xp.ps1
│   └── rgss1\    RGSS1_unpacker3b.c + RGSS1 专属 def/.S   # 若执行者判断该文件目标运行时专属则入此桶
│       # 注：RGSS1_* 文件名不改（Q17 同理，编号/前缀是身份锚点）
├── local\                           # ★整个目录不入库（.gitignore 只此一条 + 兜底）
│   ├── backup\      # ← backup\        13 个原版二进制
│   ├── unpacked\    # ← unpacked\      脱壳产物
│   ├── build\       # ← build\         构建产物（rgss2 的 *.o 与 RGSS202E_rebuild.dll、rgss3 的 build\Rubylib 迁移后见 §5）
│   ├── test\        # ← test\          530MB 游戏副本（tracked 的 14 个探针 .c 先迁 tools\，§3）
│   └── third_party\ # ← third_party\   Ruby 源码树 + tarball；README.md 留库（§3）
└── docs_rgss1_rtti_tmp.txt          # 删除（.gitignore:87 已注明"内容已并入 docs\rgss1\"，Q20）
```

**不存在的目录不建**（Q25）：`src\shared\`、`src\runtimes\rgss1\` 都不出现。RGSS1 的存在由 `docs\reports\rgss1\`、`tools\` 里的 `rgss1_` 前缀文件和 `docs\progress\rgss1.md` 表达。

## 2. 迁移总原则

1. **四刀提交**（Q12=A），每刀一个 commit，commit message 见各刀首行。刀与刀之间跑验证门（§6），门不过就修，修不动就停下报告。
2. tracked 文件用 `git mv`；untracked（local/ 五桶 600MB+）用文件系统移动。
3. **每刀做完立即修该刀波及的路径引用**（grep 新路径名验证），不留到最后一刀。
4. 原始 DLL 与游戏副本只读红线不变：移动=改名换目录，不打开写入，不删除。
5. 全程不动 `E:\rgss2-rebuild` 目录名本身（Q6）。

## 3. 刀 1 — `local/` 工作台就位

**Commit 1: `restructure: move workshop dirs into local/`**

```
mkdir local
move backup     local\backup
move unpacked   local\unpacked
move build      local\build          # 含 build\ruby-build、build\rubylib（rgss3 依赖，路径在刀 2 修）
move test       local\test           # 530MB，最后移；先移 tracked 的 14 个探针 .c（见下）
move third_party local\third_party
```

**刀 1 内的 tracked 变更**（先做，再移目录）：

| 动作 | 文件 |
|---|---|
| `git mv` | `test\Project3_orig\fprobe.c fprobe2.c fprobe3.c minitest.c tprobe.c` → `tools\probe\` |
| `git mv` | `test\Project3_rebuild\fonttest.c fonttest2.c fonttest3.c fonttest4.c mp.c` → `tools\probe\` |
| `git mv` | `test\scratch\fcmp.c fset.c fset2.c fset3.c` → `tools\probe\` |
| `git mv` | `third_party\README.md` → `third_party_README.md`（暂放根，刀 4 归 `docs\`；见 §7 注） |
| 删除 | `docs_rgss1_rtti_tmp.txt`（git rm） |
| 删除 | `docs\assets\embedded_rpg_classes.pretty.rb`（与 .rb 内容逐字节同（差换行数=行数，已验证）；Q27） |
| `git mv` | `docs\assets\embedded_rpg_classes.rb` → `src\runtimes\rgss2\embedded\rpg_classes.rb`（刀 1 先建目录放文件；它是构建输入不是文档，Q9=A） |
| `git rm -r --cached` | 若 `docs\assets\Project3_scripts\` 有任何 cached 残留 |

**风险与顺序**：先移 `backup\`、`unpacked\`（无进程占用、体积小）→ 验证 → `build\`、`third_party\` → 最后 `test\`（530MB，移动几分钟；移动前确认没有编辑器/终端 cwd 停在 test\ 内，否则 Windows 拒绝移动）。

## 4. 刀 2 — `src/` 运行时树

**Commit 2: `restructure: src/ becomes per-runtime tree with rgss2 rename`**

```
git mv src\rgss_export.c          src\runtimes\rgss2\export.c
git mv src\rgss_classes.c         src\runtimes\rgss2\classes.c
git mv src\rgss_data_classes.c    src\runtimes\rgss2\data_classes.c
git mv src\rgss_bitmap.c          src\runtimes\rgss2\bitmap.c
git mv src\rgss_graphics_classes.c src\runtimes\rgss2\graphics_classes.c
git mv src\rgss_render.c          src\runtimes\rgss2\render.c
git mv src\rgss_gamemain.c        src\runtimes\rgss2\gamemain.c
git mv src\rgss_marshal.c         src\runtimes\rgss2\marshal.c
git mv src\rgss_embedded.c        src\runtimes\rgss2\embedded.c
git mv src\rgss_regex_stubs.c     src\runtimes\rgss2\regex_stubs.c
git mv src\rgss_image.cpp         src\runtimes\rgss2\image.cpp
git mv src\RGSS202E.def           src\runtimes\rgss2\exports.def
git mv src\RGSS202E.def.txt       src\runtimes\rgss2\exports.orig.txt   # gen_def.py 的输入，含 RVA（Q20 考察：它是输入不是产物）
git mv src\ext\Win32API\Win32API.c src\runtimes\rgss2\ext\Win32API.c    # Q28=A
git mv src\m3test\host.c          src\runtimes\rgss2\selftest\ruby_battery.c   # Q8：去里程碑编号
git mv src\m3test\oem_eval.c      src\runtimes\rgss2\selftest\oem_eval.c
git mv src\m3test\ruby_test.c     src\runtimes\rgss2\selftest\ruby_test.c
git mv src\m3test\w32t.c          src\runtimes\rgss2\selftest\w32t.c
git mv src\RGSS3_300              src\runtimes\rgss3\300
git mv src\rm2k                   src\runtimes\rm2k
```

**rename 后代码内修改**（仅 rgss2 内部互引，用 grep 逐个核对）：
- 10 个 `.c` 的文件头注释/`#include` 若按文件名互引则同步改名；`embedded.c` 里 objcopy 符号引用不变（`rpg.rb` 的 `_binary_` 前缀与路径无关，但 Makefile 里的源路径要改，见 §5）。
- `exports.def` / `exports.orig.txt` 不需要内容改动。

## 5. 构建脚本（刀 2 内新增）

**`src\mk\common.mk`**（Q13=A：只放参数与模式规则，不放目标）：
- `MINGW ?= /c/msys64/mingw32/bin`、`CC := $(MINGW)/i686-w64-mingw32-gcc`（rgss3 现值）
- `CFLAGS_COMMON := -O2 -Wall -std=gnu89`（rgss3 现值；rgss2 若需 gnu99 在自己 Makefile 里叠加）
- `LDFLAGS_DLL := -shared -static-libgcc`
- `SCOPE_LOCAL ?= $(ROOT)/local`（Q10=ii：环境变量可覆写，缺省回落 `<repo>/local`）
- 模式规则：`%.o: %.c`（依赖各目标自定头文件列表，common 不管）

**`src\runtimes\rgss2\Makefile`**（新写，以 PROGRESS.md §六为蓝本，但：）
- 产物路径：`$(SCOPE_LOCAL)/build/rgss2/RGSS202E_rebuild.dll`（不再写死 `build\`）
- `embedded\rpg_classes.rb` 经 `cp` → `$(SCOPE_LOCAL)/build/rgss2/rpg.rb` → objcopy（符号名 `_binary_rpg_rb_start` 等，与现状一致）
- 链接库：`-L$(SCOPE_LOCAL)/build/ruby-build -lmsvcrt-ruby18-static`（Ruby 1.8.1 静态库现在住 `local\build\ruby-build`）
- 10 个 `.c` + `image.cpp` 逐个列出新文件名（`bitmap.c graphics_classes.c …`）
- `make clean` 只删 `$(SCOPE_LOCAL)/build/rgss2`

**`src\runtimes\rgss3\300\Makefile`**（改现有 44 行版，三处）：
- `ROOT := ../../..`（多了一层 `runtimes\`）
- `RUBYLIB := $(SCOPE_LOCAL)/build/rubylib`（引 `common.mk` 的变量；Ruby 1.9.2 库与 rgss2 的 1.8.1 库**不同路径**，已在实测确认——common 只共享工具链，不共享 Ruby 路径）
- `include ../../mk/common.mk` 替换本地的 `CC/WINDRES` 定义

**`tools\Makefile`**（新写）：目标按 §1 桶名分组（`unpack pe analysis probe verify`），每个 `.c` 一条编译规则，产物统一 `$(SCOPE_LOCAL)/build/tools/<name>.exe`。**先写 Makefile → 全量重建 → 对比产物能跑 → 才允许删旧 .exe/.o**（Q20=B 的顺序）。

**PROGRESS.md §六 与 README §四 B 的构建命令段**：刀 4 时替换为一行 `make -C src/runtimes/rgss2`，原文降级为"人读版"链接到 Makefile——**Makefile 是唯一真相**（Q4 结论）。

## 6. 刀 3 — `tools/` 分桶

**Commit 3: `restructure: tools/ staged by pipeline phase`**

按 §1 的五桶 + `rgss1` 桶执行 `git mv`。归属速查（按"目标运行时无关 → 按阶段；RGSS1 专属 → rgss1/ 或 probe/ 保留前缀"）：

- `unpack\`：`unpacker.c unpacker2.c unpacker3.c unpacker.rc unpacker.manifest`（+`RGSS1_unpacker3b.c` 若执行者认定其目标专属则入 `rgss1\`，否则 `unpack\` 保留前缀）
- `pe\`：`mkpe.py mkpe2.py impcheck.c gen_def.py rsrcwalk.py peinfo2k.py peinfo_xp.py iat_static.py`
- `analysis\`：`rtti_scan.py dumpat.py decode_rbdefs.py find_init_fns.py api_usage.py api_usage2.py pseudo_coverage.py fix_idalib_cfg.py ruby_mingw_patches.py rgss3_strdiff.py delphi_probe.py rm2k_delphi_scan.py rm2k_disasm.py lcf_syntax_probe.py probe.S probe.def probelog.c input_probe.rb`
- `probe\`：`iat_probe.c keytest.c blinkprobe.c cursortest.c probe_input_consts.c probe_input2.c rgss_introspect.c rgss_introspect2.c smoke.c difftest.c` + 刀 1 迁入的 14 个 test 探针
- `verify\`：`verify_bitmap.c verify_rebuild.c bmp2png.ps1 framediff.py verinfo_xp.ps1`
- `RGSS1_*` 15 个源码文件：带运行时语义的（findimp/iatcheck/iatscan/mkpe/patch3b/stubdump/ida_client/rtti_scan/input*/probe.S/fixstub.S/fixtick.c/probelog.c/postkey 等_ps1）入 `probe\` 保留前缀；`RGSS1_unpacker3b.c` 入 `unpack\` 或 `rgss1\` 由执行者按同判据定
- **不迁**（编译产物，留在原地由刀 1 的 local 化覆盖或直接删除）：`*.o *.exe *.dll *.log __pycache__\ RGSS202E_probe.dll unpacker.res.o`——刀 3 完成且 tools\Makefile 验证通过后，`tools\` 根下这些扩展名文件一律删除（Q20=B：untrack 但先验证可再生）

## 7. 刀 4 — `docs/` 文体分家 + 根文档重写

**Commit 4: `restructure: docs by genre; root rewritten for SCOPE`**

### 4a. 报告与 spec 分拣（依据 17 份 H1/H2 实测，narrative→reports，spec→analysis）

| 现路径 | 新路径 | 判据 |
|---|---|---|
| docs\M1_脱壳与基线报告.md | docs\reports\rgss2\ | mixed（鉴定表在，过程也在；整体叙事性强） |
| docs\M1.5_可运行脱壳版报告.md | docs\reports\rgss2\ | narrative |
| docs\M2_Game.exe接口文档.md | docs\analysis\rgss2\ | **纯 spec**（69 导出契约，零叙事） |
| docs\M2_模块地图.md | docs\analysis\rgss2\ | **纯 spec**（RVA→归属、45 vftable） |
| docs\M3_Ruby集成报告.md | docs\reports\rgss2\ | narrative（构建日志+验收） |
| docs\M4_对象层报告.md | docs\reports\rgss2\ | mixed（API 表其实已进 tsv；报告留 reports） |
| docs\M5-c_Bitmap布局与像素格式.md | docs\analysis\rgss2\ | **纯 spec**（内存布局/像素格式/方法地址） |
| docs\M5-c_图形类注册链.md | docs\analysis\rgss2\ | spec（两阶段注册链+地址） |
| docs\M5-d_Scripts解析.md | docs\analysis\rgss2\ | **纯 spec**（Marshal 4.8 容器+段表） |
| docs\M5-e_重建DLL骨架.md | docs\reports\rgss2\ | narrative |
| docs\M5-f_数据类实现.md | docs\reports\rgss2\ | mixed（期望值表其实已进 audit A1） |
| docs\M5-g_渲染与契约.md | docs\reports\rgss2\ | narrative（黑屏调试史） |
| docs\M5-i_字体系统逆向.md | docs\analysis\rgss2\ | spec（IAT VA→gdi32 实测映射+地址对照） |
| docs\M5-j_draw_text逆向.md | docs\analysis\rgss2\ | spec（ABI：thiscall 7 参、CFont 结构偏移） |
| docs\M5-k_字体创建逆向修正.md | docs\reports\rgss2\ | mixed（三根因调试叙事为主） |
| docs\M5_Init注册总表.md | docs\analysis\rgss2\ | **纯 spec**（14 类 Init 表+全量 API 映射） |
| docs\M5_主循环与Init归属.md | docs\analysis\rgss2\ | mixed，但主循环反编译全解属 spec |
| docs\交付要求.md | docs\reports\rgss2\ | rgss2 线验收标准 |
| docs\rgss1\{M1,M1.5,M2}*.md | docs\reports\rgss1\ | 原地三层上移 |
| docs\rgss3\RGSS3_*.md (4) | docs\reports\rgss3\ | 同上 |
| docs\rm2k\{M2,M3}*.md | docs\reports\rm2k\ | 同上 |

（文件名一律不改，Q17=软采纳 A：编号/前缀是交叉引用锚点，主人已说报告将来会整理重写——重写时再考虑去编号，本次不动。）

### 4b. PROGRESS.md 拆解（24KB → 三处，不留根目录文件）

| 原内容 | 去向 |
|---|---|
| §一 里程碑总览 + §二 当前状态 + §八/九/九A/十 的进度性内容 | `docs\progress\rgss2.md`（重写为看板：状态矩阵+产物清单+未决问题） |
| §五 45 条踩坑表 + "Ruby 1.8.1 C API 陷阱" 7 条 | `docs\analysis\rgss2\pitfalls.md`（长期参考事实；45 条中 37/38/39/41-45 属"原版事实"性质，与实现修正记录一并保留） |
| "Win32API 调用约定"段（§三 表内 + §五 无专条，分散在 rgss_export/Win32API 注释） | `docs\analysis\common\win32api_cdecl.md`（新写 20 行小文：MSVC 歪打正着 vs mingw cdecl/stdcall 双清栈；rgss1 建源时是 shared/ 首个候选，Q28=A） |
| §六 构建命令 | 删除，指向 `src\runtimes\rgss2\Makefile`（§5） |
| §四 关键事实表 | 已被 analysis\ 各文档覆盖，progress\rgss2.md 只留索引 |
| §七 未决问题、§十一 风险 | `docs\progress\rgss2.md` 合并入"未决"节 |
| 十·调试工具表 | `docs\progress\rgss2.md` 附录或删（工具已在 tools\ 分桶自明） |

### 4c. 三份子线 PROGRESS → docs\progress\

`git mv docs\rgss1\PROGRESS.md docs\progress\rgss1.md`（内容不动）
`git mv docs\rm2k\PROGRESS.md docs\progress\rm2k.md`（内容不动）
`git mv docs\rgss3\README.md docs\progress\rgss3.md`（内容不动；rgss3 失去 README 后由根 README §状态矩阵覆盖）

### 4d. 根 README.md 重写（Q14=A，Q19=A，Q23=A）

骨架（约 120 行内）：
1. 标题：`# SCOPE — Source-Code Oriented rePlays Engine`？**否**——主人定名原文照录：**"SCOPE — Source-Code Oriented role-Play game Engine. A source-level reconstruction of classic RPG Maker runtimes for 1:1 behavioral fidelity."**（主人 2026-10-27 原话；定位是 **Project** 不是 Engine 实体，Q31 补记）
2. 一段话：这是什么计划、1:1 行为保真、drop-in DLL 交付形态
3. **四运行时状态矩阵**：rgss2→M5-k / rgss3→M3 / rm2k→M3 / rgss1→M2（以 docs\progress\ 为准）
4. 仓库边界一节（承 .gitignore 六段注释的内容）：哪些在 local\、为什么、SCOPE_LOCAL 环境变量
5. 快速开始：装 msys64 mingw32 → `make -C src/runtimes/rgss2` → `tools/…smoke` → `local/test/Project3_rebuild/Game.exe`
6. 文档地图：reports/analysis/audit/progress/adr 五目录一段话
7. 许可节（LICENSE 同步新增）：
   - 本仓库源码与文档：**All Rights Reserved**（主人明确暂保留全部权利，Q19）
   - 不含任何 Enterbrain 二进制；Ruby 1.8.1/1.9.2 上游按 Ruby License，取法与 SHA-256 见 `third_party_README.md` → 归档后 `docs\analysis\common\third_party.md`
   - 原版行为事实（地址、布局、IAT）为观察记录，不受本仓库许可约束
   - "给后人研究"表述改为"公开可读、供研究参考，不授权复用"（Q23）

### 4e. .gitignore 重写（Q21=A 按需精简）

```gitignore
# 工作台：一切可再生成物与著作权物（原因详见 README「仓库边界」一节）
/local/

# 兜底：防止在 local/ 之外误产生产物
*.o
*.obj
*.a
*.lib
*.exe
*.dll
*.pdb
*.ilk
*.exp
*.i64
*.idb
*.id0
*.id1
*.id2
*.nam
*.til
__pycache__/
*.pyc
*.pyo
*.log
*.swp
*~
Thumbs.db
desktop.ini
.vs/
.vscode/
```

（原 96 行六段说明迁入 README §仓库边界；`docs_rgss1_rtti_tmp.txt`、`/docs/assets/Project3_scripts/`、`/backup/` 等条目随目录消失而删除。）

### 4f. LICENSE 新增

一句话版：`Copyright (c) 2026 SCOPE project authors. All rights reserved. Not for redistribution.` + 三行边界声明（同 4d.7）。

## 8. 验证门（每刀之后）

| 门 | 命令 | 通过标准 |
|---|---|---|
| G1（刀 1 后） | `ls local/{backup,unpacked,build,test,third_party}`；`git status` 仅显示预期 rename/delete | 五桶齐全；无意外 untracked |
| G2（刀 2 后） | `make -C src/runtimes/rgss2` | 产出 `local/build/rgss2/RGSS202E_rebuild.dll`，69 导出（`objdump -p … | grep -c ordinal` 或现有 impcheck） |
| G3（刀 2 后） | `local/build/tools/smoke.exe local/build/rgss2/RGSS202E_rebuild.dll` | LoadLibrary 冒烟通过 |
| G4（刀 2 后） | `src/runtimes/rgss2/selftest` 电池（编译 host→`ruby_battery`，载入重建 DLL） | Ruby 层 7/7 对拍（与 PROGRESS §四 C 同判据） |
| G5（刀 3 后） | `make -C tools` | tools 全部源文件可编译（允许个别工具因缺 DLL/环境跳过，逐个记录） |
| G6（刀 4 后） | 全库 `grep -rn "backup/\|unpacked/\|build/full_disasm\|third_party/ruby\|docs/assets/embedded"`，排除 local\ 与本文件 | 0 命中（引用已全部改写；本文件归档后自身除外） |
| G7（最终） | `local/test/Project3_rebuild/Game.exe`（真实游戏，标题→进图） | 与重构前行为一致 |

**回滚**：任一门连续两次修复失败 → `git reset --hard` 到该刀之前，报告主人。

## 9. 已知陷阱（执行会话必读）

1. **.gitignore 重纳失效**：`/local/` 整目录排除后，git 不会进入该目录，`!/local/test/**/*.c` 类规则**无效**——这就是 14 个探针 .c 必须在刀 1 迁出的原因。若执行中新增 tracked 文件到 local/ 需求，停下来问主人。
2. **600MB 文件移动**：test\ 移动前确认无 shell/编辑器 cwd 占用；Windows 移动中断会留半搬状态，重试前先对账 `find` 两边文件数。
3. **文档路径改写量**：17 份报告 + progress 三份 + audit 三份里大量 `backup\…`、`unpacked\…`、`build\full_disasm.txt`、`docs\assets\…` 引用。刀 4 的 G6 只查"结构性"引用；报告正文里的历史路径**不改写**（它们是当时的事实记录，改了反而失真）——只在每份报告顶部加一行脚注："本文写于仓库重构前，`backup/` 等路径现位于 `local/` 下"。**这个脚注方案是本工单对 G6 的正式豁免解释**，grep 允许 docs\reports\ 与 docs\analysis\ 中的历史路径命中。
4. **rgss2 Makefile 的 objcopy 符号**：`rpg.rb` → `_binary_rpg_rb_start/_binary_rpg_rb_end`，符号名取决于**文件名**（`rpg.rb`），不能改成 `rpg_classes.rb` 再 objcopy（否则 `embedded.c` 里的 extern 对不上）；Makefile 里保留 `cp … build/rpg.rb` 这一步正是为此。
5. **rgss3 的 Ruby 库路径**：`local/build/rubylib`（1.9.2）与 rgss2 的 `local/build/ruby-build`（1.8.1）是**两个不同的目录**，common.mk 不吸收它们（实测自 src\RGSS3_300\Makefile 与 PROGRESS §六）。
6. **编译产物删除顺序**：刀 3 只 `git mv` 源码；`tools\` 下 `.exe/.o/.dll/.log` 与 `__pycache__` 在 tools\Makefile 全量重建验证通过（G5）之后才允许物理删除；`src\m3test\*.exe/*.dll` 同理（刀 2 后验证 G4 通过即删）。
7. **`exports.def.txt` 改名 `exports.orig.txt`** 是改名不是删除：它是 `tools/pe/gen_def.py` 的输入（含 RVA 列），执行时 grep `gen_def.py` 确认它读的文件名并同步。
8. **本文件自身的归宿**：全部 4 刀完成 → 文首加"已完成（日期）"→ `git mv` 到 `docs\adr\0001-repository-restructure.md` → commit 5 `docs: archive restructure plan as ADR-0001`。

## A. 决策记录（Q1–Q31 简表）

| Q | 决议 |
|---|---|
| 1 | 四运行时并列、彼此独立，仅工具链/思路互参（A） |
| 2 | 工作台收进 `local/`（C） |
| 3 | docs 按文体分家（C） |
| 4 | 每运行时独立 Makefile（B），公共参数 `src/mk/common.mk`（Q13 合成） |
| 5 | tools 按流水线阶段分桶（A） |
| 6 | 迁移=分桶提交；**工作目录名不改** |
| 7 | 源码文件去 `rgss_` 前缀（A）；rgss3/rgss2 实现大概率不共用 |
| 8 | m3test → `rgss2/selftest/`，host.c 改名 ruby_battery.c（A） |
| 9 | 构建输入进运行时子树（A）；`embedded/rpg_classes.rb` |
| 10 | `local/{backup,unpacked,build,test,third_party}` + `SCOPE_LOCAL` 环境变量缺省回落（A+ii） |
| 11 | `tools/RGSS1_*` 摊入各桶保留前缀（A）；"与本项目无关"说法随 SCOPE 作废 |
| 12 | 四刀提交（A） |
| 13 | `mk/common.mk` 只放工具链变量与模式规则（A） |
| 14 | README 首句 SCOPE 项目定位 + 状态矩阵（A） |
| 15 | ~~全局按运行时粒度~~ → 15r：每运行时自决版本粒度（300/301 差异可能大，主人纠正）；判据入 ADR |
| 16 | 进度文件归一（A→被 26 修正为 C） |
| 17 | 报告文件名保留编号（软采纳；将来整理重写时再议） |
| 18 | audit 独立文体目录（A） |
| 19 | 暂保留全部权利（→23 落地为 LICENSE All Rights Reserved） |
| 20 | tools 编译产物：先验证可再生，再删（B）；`docs_rgss1_rtti_tmp.txt` 删除 |
| 21 | .gitignore 按需精简（骨架 `/local/` 一条 + 兜底段） |
| 22 | 45 条踩坑 → `docs/analysis/rgss2/pitfalls.md`（主人授权执行者定） |
| 23 | LICENSE All Rights Reserved + README 边界声明（A）；"给后人研究"改写为"公开可读、不授权复用" |
| 24 | test\ 的 14 个探针 .c 迁 `tools/probe/`（A） |
| 25 | `src/` 有啥建啥；rgss1 不建空目录（A） |
| 26 | 进度统一 `docs/progress/<rt>.md`（C） |
| 27 | pretty.rb 为纯格式化副本（已验证：字节差=行数）→ 删；.rb 为构建输入迁 `src/runtimes/rgss2/embedded/` |
| 28 | Win32API.c → `rgss2/ext/`，标注 shared/ 首个候选（A） |
| 29 | rm2k 暂不分 2000/2003（C），决定入 ADR |
| 30 | 先落本工单（C），主人提交其他会话执行 |
| 31 | 本文件放根目录，执行完归档 `docs/adr/0001`（A） |
