# RGSS3 线（RGSS300.dll / RPG Maker VX Ace）— 章程与进度

> 三线并行之一（RGSS1=XP 线 / RGSS2=VX 线（见根 README.md）/ **RGSS3=VX Ace 线（本目录）**）。
> 各线独立交付；工具链（tools/）与第三方源码（third_party/）共享。

## 一、本线共识（2026-10-26 用户确认，Q1–Q14）

| # | 决策项 | 结论 |
|---|--------|------|
| 1 | 文档/目录 | 本线文档全部进 `docs/rgss3/`（RGSS3_ 前缀）；产物 `unpacked/RGSS3_*`；源码 `src/RGSS3_300/`；构建 `build/RGSS3_300/`；根目录 README/PROGRESS 属 202E 线，**不改动** |
| 2 | 还原对象 | **RGSS300.dll 唯一**；RGSS301 仅轻量 diff 参考（实测为同源完全重编译版本，字节级不可对照） |
| 3 | 地面真值 | **backup/RGSS300.dll（汉化版，带壳）= 行为基准**（含汉化行为；真原版难找、差异仅字符串层面，用户背书）。自家纯净脱壳件为准；日后找到真原版只参考不回改 |
| 4 | RGSS300_（脱壳修改版） | 仅分析加速器；其修改已 100% 鉴定（M1 §2）：分辨率 640×480→1024×768、F12 屏蔽、Font.default_name nsimsun→simhei；"解密函数修改"**证伪**；资源=官方日英血统（非汉化版脱壳） |
| 5 | 技术路线 | 混合式：官方 **Ruby 1.9.2p0** 源码编译进 DLL + REA/IDA 手工还原 RGSS 层；Enterbrain 补丁逐文件 diff 清单 |
| 6 | 交付 | 纯 C、mingw32（C:\msys64）、32 位同名 drop-in RGSS300.dll、27 导出 1:1；壳（也是 aspack）不还原 |
| 7 | 测试 | 基线 = 带壳汉化版实跑；硬指标 = 逐帧截图 diff + RGSSEval 电池；Marshal 字节一致为参考项；人工试玩终验（阈值 M6 定标）。测试游戏 = test/Project1（只用副本） |

## 二、里程碑

| 里程碑 | 内容 | 状态 |
|--------|------|------|
| RGSS3-M1 | 基线：脱壳/可运行件/修改鉴定/301 轻量 diff/Ruby 源码就位 | ✅ 2026-10-26（[报告](RGSS3_M1_基线与修改鉴定报告.md)） |
| RGSS3-M2 | RTTI 类枚举 + 模块地图 + 27 导出接口文档 + 特殊导入结案 | ✅ 2026-10-26（[接口文档](RGSS3_M2_Game.exe接口文档.md) / [模块地图](RGSS3_M2_模块地图.md)） |
| RGSS3-M3 | Ruby 1.9.2p0 静态库构建 + 集成 + 内建脚本取证 | ✅ 2026-10-26（[报告](RGSS3_M3_Ruby集成报告.md)） |
| RGSS3-M4 | RGSS 对象层还原（288 API + 45 内建类） | ⬜ 下一个 |
| RGSS3-M5 | 图形/音频后端镜像 | ⬜ |
| RGSS3-M6 | 自动对照测试 + 地址映射表 | ⬜ |

## 三、本线文件索引

- `RGSS3_M1_基线与修改鉴定报告.md` — OEP/脱壳/可运行件/修改全归因/301 重编译结论
- `unpacked/RGSS3_300_pristine/` — 地面真源（OEP 纯净 dump，SHA-256 ff469522…f1cf05）
- `unpacked/RGSS3_300_probe.dll` — 可运行重建 PE（已实跑 Project1 12s）
- `unpacked/RGSS3_301_v1/` — 301 分析镜像（.text/.rdata 有效）；`RGSS3_301_pristine/` 为废件（见 NOTE.txt）
- `test/Project1_orig|probe/` — 基线/探针副本（均实跑通过）
- `third_party/ruby-1.9.2-p0/` — 官方源码（SHA-256 8c0c4e26…285c）
