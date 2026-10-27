# RGSS202E.dll 逆向重建项目

> 目标：把 ASPack 壳下的 RGSS202E.dll（RPG Maker VX 运行库）完整逻辑用 C 重建，
> 成为 32 位 drop-in 替换 DLL，并与原版对照测试通过。壳本身不还原。

## 零、仓库边界（先读这个）

本仓库**只收录源码与文档**，约 2.5MB。以下内容不进版本库，但仍留在本地工作区：

| 排除项 | 原因 |
|--------|------|
| `backup/` `unpacked/` | 原版 RGSS*.dll 及其脱壳派生物，Enterbrain 著作权，不公开分发 |
| `build/` | 构建产物，可由源码重新生成 |
| `test/` | 530MB 游戏副本（含 RTP 素材、字体、音频）；**测试/探针的 `.c` 源码仍保留** |
| `third_party/ruby-*` | 81MB 公开上游代码，取法与 SHA-256 见 `third_party/README.md` |
| `docs/assets/Project3_scripts/` | 从游戏 Scripts.rvdata 反编译的 95 段脚本原文 |

完整过滤规则见 `.gitignore`。

保留 `docs/assets/embedded_rpg_classes.rb` 是个例外：它是 DLL 内嵌 RPG 类源码，
且是重建构建的**必需输入**（objcopy 打成 `build/rpg_src.o` 链进 DLL）。
没有它无法构建，因此按"可复现性优先"保留。

## 一、项目要求（共识已锁定）

| # | 决策项 | 结论 |
|---|--------|------|
| 1 | 还原对象 | 仅 RGSS202E.dll（VX 系列 2010-09-02 版）；四个版本中先做一个，其余日后 diff 补 |
| 2 | 技术路线 | **混合式**：Ruby 1.8.1 官方开源源码编译进 DLL（解释器层），REA/IDA+Hex-Rays 手工还原 RGSS 专属层（对象/图形/音频/主循环） |
| 3 | 交付形态 | 同名 drop-in DLL，导出表 1:1（69 导出），Game.exe 不动，32 位（mingw32 gcc 12.2，`C:\msys64`） |
| 4 | 测试标准 | 自动化：Project3 副本逐帧截图 diff + Ruby eval 脚本电池为主；Marshal 字节一致作参考项不作硬门槛；人工试玩终验 |
| 5 | 图形/音频 | 镜像原版实现（已鉴定：**纯 GDI+DIB 渲染、winmm MCI+ACM 音频，无 DirectX**） |
| 6 | 用途定位 | 偏"给后人研究"，按可打补丁基线精度做，全链路保真 |
| 7 | 工具 | REA + IDA Pro 9.4（headless idalib，Hex-Rays 可用）；允许下载开源源码；Ghidra 仅备胎不预装 |
| 8 | 节奏 | 里程碑制：M1 脱壳 → M1.5 可运行脱壳 → M2 模块地图 → M3 Ruby 集成 → M4 RGSS 对象层 → M5 图形音频 → M6 对照测试 |
| 9 | 工作区 | 本仓库根目录（原始 DLL 与游戏原件均在本地只读区，不入库；测试只用 `test/` 下副本） |

## 二、测试环境

- 测试游戏副本：`test/Project3_orig\`（Game.ini 已指向 RGSS202E.dll，RTP 为空）
- 对照基线：**原版带壳 DLL**（backup/RGSS202E.dll）
- 已知：游戏目录同名的 RGSS202J_fixed.dll 是坏的（用户确认），忽略
- 注意：本机 Win11 对 32 位进程强制注入 apphelp 垫片，详见 M1.5 报告 §4

## 三、目录结构

```
<仓库根目录>\
├── README.md            本文件（要求与环境）
├── PROGRESS.md          进度看板（每里程碑更新，含完整构建命令与踩坑记录）
├── docs\                各里程碑报告 + 研究文档
│   └── assets\          分析资产（328 API 表 / 内嵌 RPG 源码 / 95 段游戏脚本 / 截图）
├── backup\              四个原版 DLL（只读，不修改）
├── unpacked\            脱壳产物
│   ├── RGSS202E\          v1 分析 dump（.data 脏，仅供导入映射）
│   ├── RGSS202E_pristine\ v3 纯净 dump（OEP 快照，构建可运行版的原料）
│   ├── RGSS202E_unpacked.dll  分析版 PE（给 IDA / objdump）
│   ├── RGSS202E_probe.dll     可运行脱壳版（需配 rgssprobe.dll）
│   └── RGSS202E_runnable.dll  可运行脱壳版（最终版，无探针依赖，独立可用）
├── third_party\ruby-1.8.1\  Ruby 1.8.1 官方源码（tar.gz SHA-256 d0f1e19f…de4e）
├── tools\               全部自研工具（脱壳/PE合成/RTTI/API解码/rvdata/构建）
├── test\                Project3_orig（基线）+ Project3_rebuild（★重建 DLL 测试）+ Project3_probe
├── src\                 ★重建源码树（12 个 C/C++ 文件 + .def）
└── build\               构建产物（RGSS202E_rebuild.dll 3.56MB + 全部 .o）
```

## 四、复现流水线

### A. 脱壳（已固化，产物在 unpacked/）
```
1. unpacker3.exe backup\RGSS202E.dll unpacked\RGSS202E_pristine E6E94   # 调试器+硬件断点, 冻结于 OEP
2. copy v1 会话的 modules.txt / modmaps.txt → pristine 目录             # 同 boot 内系统模块基址一致
3. py tools\mkpe2.py unpacked\RGSS202E_pristine out.dll 0xE6E94 [--probe]
4. tools\impcheck.exe out.dll      # 导入逐项可解析
5. tools\smoke.exe out.dll         # LoadLibrary 冒烟
```

### B. 重建版 DLL 构建（★当前主力，完整命令见 PROGRESS.md §六）
```
g++ -shared -O2 -static -Wl,--image-base,0x10000000 -Wl,--disable-dynamicbase \
    -o build/RGSS202E_rebuild.dll build/*.o src/RGSS202E.def \
    -L build/ruby-build -lmsvcrt-ruby18-static \
    -lws2_32 -lkernel32 -luser32 -lgdi32 -lwinmm -lmsacm32 -ladvapi32 -lole32 -lz -lgdiplus
```

### C. 验证
```
1. src/m3test/host.exe  build/RGSS202E_rebuild.dll   # Ruby 层电池
2. tools/smoke.exe      build/RGSS202E_rebuild.dll   # 加载冒烟
3. test/Project3_rebuild/Game.exe                     # ★真实游戏（标题画面）
```

## 五、文档索引

**里程碑报告**
- [docs/M1_脱壳与基线报告.md](docs/M1_脱壳与基线报告.md) — 脱壳、鉴定（Ruby 1.8.1 / GDI / 69 导出 / RTTI）
- [docs/M1.5_可运行脱壳版报告.md](docs/M1.5_可运行脱壳版报告.md) — OEP、IAT 重建三层坑、脏 .data 与调试器方案
- [docs/M2_Game.exe接口文档.md](docs/M2_Game.exe接口文档.md) — 69 个导出语义
- [docs/M2_模块地图.md](docs/M2_模块地图.md) — 三层架构 / 45 vftable / 区段归属
- [docs/M3_Ruby集成报告.md](docs/M3_Ruby集成报告.md) — Ruby 1.8.1 mingw32 构建与对拍
- [docs/M4_对象层报告.md](docs/M4_对象层报告.md) — 注册链 / Run 骨架 / API 全表 / 内嵌源码
- [docs/M5_主循环与Init归属.md](docs/M5_主循环与Init归属.md) — 主循环全解（F12=Reset）
- [docs/M5_Init注册总表.md](docs/M5_Init注册总表.md) — 14 类 Init 地址 + 328 API 机制
- [docs/M5-c_图形类注册链.md](docs/M5-c_图形类注册链.md) — **两阶段注册**（关键）
- [docs/M5-c_Bitmap布局与像素格式.md](docs/M5-c_Bitmap布局与像素格式.md) — ARGB8888 布局规格
- [docs/M5-d_Scripts解析.md](docs/M5-d_Scripts解析.md) — Scripts.rvdata 格式与 95 段源码
- [docs/M5-e_重建DLL骨架.md](docs/M5-e_重建DLL骨架.md) — 69 导出 + Ruby 对拍 7/7 + 认知修正
- [docs/M5-f_数据类实现.md](docs/M5-f_数据类实现.md) — 数据类行为一致验证
- **[docs/M5-g_渲染与契约.md](docs/M5-g_渲染与契约.md)** — **★黑屏修复 + 标题画面 + Window + 中文★**
- [docs/交付要求.md](docs/交付要求.md) — D1–D4 验收条款

**进度**：[PROGRESS.md](PROGRESS.md) — 含完整构建命令、踩坑记录、下一步清单
