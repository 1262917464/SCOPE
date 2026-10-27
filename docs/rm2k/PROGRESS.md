# RM2K / RM2K3 引擎重建 —— 进度看板与共识

> 与 RGSS 线（`docs/rgss1/`、`docs/rgss3/`、VX 线）**完全独立**，互不干扰。
> 本线所有产出在 `docs/rm2k/`、`src/rm2k/`、`build/rm2k/`、`test/rm2k_*` 内。
> `backup/` 只读共用。

## 一、共识（已锁定）

| # | 决策项 | 结论 |
|---|--------|------|
| 1 | 还原对象 | `backup/2000.exe`（官方日版，Delphi 5）、`backup/2003.exe`（1.0.8，Delphi 7）、`backup/2003Steam.exe`（1.1.2.1 KADOKAWA，Delphi 7）三个 exe |
| 2 | 技术路线 | **纯 C 重写**。无脚本层可借（RM2K 系不存在 Ruby 一类的东西），无开源实现可填，全部手写 |
| 3 | 代码库拓扑 | **两个工程**：`rm2k`（2000）+ `rm2k3`（2003，Steam 作变体开关）。2000↔2003 代码重合仅 0.8%，不合并 |
| 4 | 保真粒度 | **行为等价为硬标准，并复刻原版怪癖**（随机数流、定时器抖动、整数回绕、求值顺序）。不追求位级等价 |
| 5 | 交付形态 | 三个独立同名 drop-in exe（`RPG_RT.exe` / `RPG_RT.EXE`），内核以静态库组织 |
| 6 | 模块划分 | **1 Pascal 单元 = 1 对 `.c/.h`**，保留原名。`PACKAGEINFO` 是权威模块表 |
| 7 | 完整性标准 | 全部单元逐函数还原，含 `LS_Debug`、`HMapEditUtils` 等开发残留 |
| 8 | 第三方补丁 | 定为**外部边界**：只还原官方引擎，DynRPG/Cherry/Harmony 等以"钩子点 + ABI 契约"文档化；重建版必须能加载这些外部 DLL |
| 9 | 音频 | **全部后端都还原**：`DirectSound` / `aWavePlayer` / `MP3Player` / `DirectShow` / `JMidiPlay` / `hAudioHarmony` / `GMMSystem`；解码层可借现成库 |
| 10 | LCF 数据层 | 自研纯 C 实现；`liblcf` 与 EasyRPG 仅作**交叉验证参照**，不作依赖 || 11 | 工具链 | IDR（结构恢复）+ 自写 VMT/RTTI 扫描器 + IDA Hex-Rays（函数体）；Ghidra 备胎 |
| 12 | 顺序 | 先 `2000.exe`（最干净、单元最少），后 `2003` + Steam diff |
| 13 | 测试载体 | 冒烟：`test/2000`、`test/2003`（加 RTP）；功能测试另建，等标题和地图能进了再定 |
| 14 | 测试通道 | 双轨：帧缓冲哈希 diff（硬门槛）+ 探针 DLL 导出内部状态（定位工具）；运行时用真定时器，测试时用虚拟时钟 |
| 15 | 随机数 | 精确复刻 Delphi `RandSeed` LCG（乘数 `0x08088405`）；禁止使用 C `rand()` |
| 16 | 定时器 | 精确复刻 `timeSetEvent` + 独立线程，接受同样的抖动与竞态 |

## 二、为什么技术路线和 RGSS 系不同

| | RGSS 系（XP/VX/VX Ace） | RM2K 系（2000/2003） |
|---|---|---|
| 形态 | **DLL**（`RGSS*.dll`），宿主 Game.exe 不动 | **单文件 exe**，无宿主 |
| 编译器 | MSVC + 完整 RTTI | **Delphi 5 / 7**（Borland VCL） |
| 脚本层 | Ruby 1.8.1 / 1.9 / 2.x 解释器内嵌 | **无脚本层**，全编译进 exe |
| 数据格式 | Marshal 序列化 Ruby 对象 | **LCF** 二进制块格式 |
| 可借力 | Ruby 官方源码填掉整层 | **零可借**，全部手写 |
| 脱壳 | ASPack / UPX 变体 | **无壳** |
| 代码量 | 数十万行 DLL | ~4000–6500 函数 / exe |

关键推论：RGSS 线的"混合式（官方 Ruby 源码 + 手写 RGSS 层）"在这里**完全不成立**。RM2K 没有可以填坑的上游开源实现，必须从汇编逐函数还原。

## 三、里程碑

| # | 里程碑 | 产出 | 验收标准 | 状态 |
|---|--------|------|----------|------|
| M1 | 基线鉴定与骨架 | 三 exe 的单元表/VMT/类图/published 方法/DFM 全量导出；扫描器跑通 | 单元表与 `PACKAGEINFO` 完全对齐；类层次无断链 | ✅ **完成** |
| M2 | 窗口与主循环 | `TFormLcfGameMain` + `TAuroraDraw` 帧循环 + 输入 | 空画面能启动、响应按键、正常退出 | ✅ **完成** |
| M3 | LCF 数据层 | `DataBaseObj`/`GameDataUnit`/`oLD_MapTree`/`vLD_Map` 全量读写 | `test/2000`+`test/2003` 全部 LDB/LMU/LMT 解析正确，字节级 round-trip | ✅ **解析完整**（6/6 通过；写回待补） |
| M4 | 图形核心 | `AuroraSheet`/`IAuroraDraw`/`DirectDraw`/`AuroraPng`/`tAuroraZlib`/`PAuroraXyz` | 地图图块、行走图、远景正确绘制 | ⬜ |
| M5 | 场景与事件 | `LS_Field`/`LS_Menu`/`LS_Shop`/`LS_Name`/`LS_SaveFile`/`iLS_Title`/`LS_GameOver` + 事件解释器 | 能到标题、能进地图、能触发事件、能存读档 | ⬜ |
| M6 | 战斗与音频 | `LS_Battle` 全家 + 全部音频后端 | 完整战斗一场；音乐音效正确 | ⬜ |
| M7 | 对照收敛 | 帧哈希 diff + 探针双轨；2003↔Steam diff；文档 | 全测试集帧哈希一致；外部 DLL ABI 兼容 | ⬜ |

排序依据：M3 先于 M4（没数据层画不出地图）；M6 把战斗与音频捆在一起（都是最后一块）。

## 四、工具与资产

```
tools/rm2k_delphi_scan.py   Delphi 结构恢复扫描器（节表/单元表/VMT/类图/published方法/DFM）
tools/rm2k_disasm.py        函数级反汇编（capstone）+ Delphi 寄存器调用约定识别 + IAT/字符串标注
tools/rsrcwalk.py           完整资源树遍历
tools/delphi_probe.py       PACKAGEINFO / DVCLAL / DFM 原始导出
build/rm2k/2000.json        2000.exe 全量结构（128 单元 / 324 类 / 1 窗体）
build/rm2k/2003.json        2003.exe 全量结构（150 单元 / 372 类 / 1 窗体）
build/rm2k/2003Steam.json   2003Steam.exe 全量结构（150 单元 / 374 类 / 1 窗体）
src/rm2k/                   M2 起的 C 实现（rm2k_delphi/GameMain/platform_win32）
```

### 构建命令

```
export PATH="/c/msys64/mingw32/bin:$PATH"     # 必须，否则 cc1 找不到 libisl-23.dll 且静默失败
gcc -m32 -std=c11 -O2 -Wall -Wextra -I src/rm2k -c src/rm2k/<file>.c -o build/rm2k/obj/<file>.o
gcc -m32 -o build/rm2k/rm2k_shell.exe build/rm2k/obj/*.o -lgdi32 -luser32
```

## 五、风险

1. **量级**：2000 约 3400 个直接调用目标（下界）、2003 约 4100；按每函数 35 行估，合计约 35 万行 C。这是年量级工程。
2. **Hex-Rays 对 Delphi 不友好**：寄存器传参（`eax`=Self、`edx`/`ecx`=参数），大量方法无 `push ebp` 帧，默认反编译质量差。需要先做调用约定标注。
3. **外部 DLL ABI**：`Harmony.dll`（DS2 自带）、`ultimate_rt_eb.dll`（Steam 版）要拿到引擎内部指针，重建版必须暴露兼容的结构布局。
4. **没有可借的上游实现**：不像 RGSS 能嵌 Ruby，这里每一个算法都要从汇编还原。
