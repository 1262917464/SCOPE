# RGSS202E（VX 线）进度看板

> 承接重构前根 `PROGRESS.md` 的看板职能（2026-10-27 重组）。约定：每完成一个里程碑，更新本文件并在 `docs/reports/rgss2/` 出报告。
> 长期参考事实在 `docs/analysis/rgss2/`（pitfalls / api_map / 布局规格）；构建命令在 `src/runtimes/rgss2/Makefile`（唯一真相）。

## 里程碑总览（截至 M5-k）

| 里程碑 | 内容 | 状态 |
|--------|------|------|
| M1–M4 | 脱壳 → 模块地图 → Ruby 1.8.1 集成 → 对象层主干 | ✅ 完成 |
| M5-a…M5-n | 主循环 / Init / 两阶段注册 / Bitmap / Scripts / 骨架 / 数据类 / 渲染 / 输入 / 字体 / draw_text / 转场与 fade / 文件转场遮罩 | ✅ 完成 |
| M5-k* | 文字混合器白盒还原（F[a]=a+1 查表式，阴影偏淡修正）+ 字体集合截断修复 | ✅ 完成 |
| M5-l | review+整修线：RGSSGameMain 对齐（F12 Reset / $TEST / 主循环架构 / B49 清理链 / B43 exit 0 / B44 日志改造+崩溃转储） | ✅ 完成（2026-10-28，销账见差异清单 §四） |
| M6 | 自动对照测试 + 研究文档 + 地址映射表 | ⬜ 未开始 |

## 当前状态（一句话）

**重建版 DLL 已能从标题画面一路进入游戏地图**：标题渲染、`Input.trigger?` 采键、
`Scene_Title → Scene_Map` 切换、地图图块与角色精灵渲染、光标闪烁、中文 draw_text
（GetGlyphOutlineW 自绘字形 + 原版 F[a]=a+1 查表混合）均按原版路径还原；
文字粗细/阴影与原生对齐（近白像素比 0.997，暗环带 B 96.1 vs 93.0）。

验证截图：`local/test/Project3_rebuild/kt_00_title.png`、`kt_01_after_enter.png`；
对比图 `docs/assets/window_9slice_compare.png`；原版基准 `local/test/Project3_orig/kt_01.png`。

## 未决问题（原 §七）

1. `IsWindowVisible` 定名 85% 置信 → M6 长会话探针可最终确认。
2. JPEG 解码器具体库（版本串 "2.0.8"）、Ogg 版本串 "0.6.0" 对号。
3. Enterbrain 对 Ruby 1.8.1 的本地补丁范围（M3 对拍 5 条全一致，M6 需全量电池）。
4. ~~`transition` 淡入淡出~~（M5-m/M5-n 已完成：freeze/transition/fadein/fadeout
   与文件转场遮罩均按原版语义实装，见 M5-n 报告）。
5. **自动图块 48 形状 → 四分之一片映射表未还原**（当前用邻接近似）。
   已实测锚点：id 2048→kind42→A1#10=深海；id 1537→kind32→A1#0=海滩；
   kind 0-31→TileA2，32-47→TileA1，48-79→TileA3，80+→TileA4。
   反汇编材料：`local/build/full_disasm.txt`（离线 objdump，基址 0x10000000）。
6. 音频（MCI/DirectSound）未实现。
7. ~~鼠标控制~~ **更正（2026-10-28）**：原版无鼠标支持。0x13940 前半段实为**手柄摇杆→方向映射**（joyGetPosEx，见差异清单 S8/S9 销案记录）。

## M5-h 剩余（原 §八）

1. 自动图块 48 形状表（见上 §未决 5，反汇编原版 Tilemap 绘制函数取 48 形状 → 2x3 块 6 片精确映射）。
2. 音频：MCI（winmm）BGM/ME + SE。
4. ~~帧率限制~~（QueryPerformanceCounter 已完成）；~~draw_text 自绘~~（M5-j 已完成）。

## 已沉淀的专题文档（不再在本看板重复）

| 主题 | 文档 |
|------|------|
| 字体系统逆向（IAT 实测 + 两段式字体创建 + 地址标注） | `docs/analysis/rgss2/M5-i_字体系统逆向.md` |
| draw_text 自绘字形光栅化（GGO + REPLACE + per-pixel alpha） | `docs/analysis/rgss2/M5-j_draw_text逆向.md` |
| 字体创建与字体名选择修正（含 48 形状表前置知识） | `docs/reports/rgss2/M5-k_字体创建逆向修正.md` |
| Graphics 转场与 fade（freeze/transition/fadein/fadeout 逐指令还原） | `docs/reports/rgss2/M5-m_Graphics转场与fade逆向.md` |
| 文件转场遮罩解码与实装（DirectDraw7 架构 + 0x100a5710/0x1009fe00 全链） | `docs/reports/rgss2/M5-n_文件转场遮罩解码与实装.md` |
| 45 条缺陷 + Ruby C API 陷阱 | `docs/analysis/rgss2/pitfalls.md` |
| Game.exe 调用契约 / Bitmap 布局 / 两阶段注册 | `docs/analysis/rgss2/` 各 spec |
| 一致性审计（A1–A5 + 总报告） | `docs/audit/` |

## 调试工具速查（原 §十）

| 工具 | 用途 |
|------|------|
| `tools/probe/keytest.c` | 自动化按键测试：启动→截图→keybd_event 注入→再截图 |
| `tools/probe/iat_probe.c` | **进程内读原版 DLL 的 IAT 并反查真实函数名**（IAT 猜错的教训） |
| `tools/probe/blinkprobe.c` | 定时截屏测光标闪烁波形（三角波/40帧/128..255） |
| `tools/probe/cursortest.c` | 验证光标移动与闪烁 |
| `tools/probe/probe_input2.c` / `probe_input_consts.c` | Input 模块状态/常量探针 |
| `local/test/Project3_rebuild/inprobe2.exe` `mapprobe.exe` | 完整流程探针 / 运行中地图数据 |
| `local/test/Project3_orig/tprobe.exe` | 用原版 DLL 实测 Table._dump 格式 |

注意：`Input` 常量在 Phase B（`RGSSGameMain`）才注册；探针必须先建窗口 + 主线程再 eval。

## 风险与注意事项（原 §十一）

- REA 的 IDA headless worker 偶尔掉线 → close_binary + open_binary 重连。
  **更好的办法：完全用离线 objdump**（`local/build/full_disasm.txt`），不依赖 IDA 在线状态。
- 批量反编译偶发单点失败（如 RGSSGameMain@0x100028C8）→ 用 procedure_assembly 手工还原。
- **Bash heredoc 里写 C 代码时 `\n` 会被转义吃掉** —— 改用 Write/Edit 工具写文件，或 `fputc(10, f)`。
- 原始 DLL 与游戏原件只读不写；测试一律用 `local/test/` 下副本。
- rgss1 / rgss3 / rm2k 三线在同仓库并行，工具与文档已分桶，不要跨线改文件。
