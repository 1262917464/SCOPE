# RGSS3 M1 报告 — 基线、脱壳与修改鉴定（RGSS300.dll）
> 注：本文写于 2026-10-27 仓库重构前；文中 `backup/` `unpacked/` `build/` `test/` `third_party/` 等路径现位于 `local/` 下，`src/rgss_*.c` 现为 `src/runtimes/rgss2/*.c`，`tools/` 已按流水线分桶（unpack/pe/analysis/probe/verify）。


日期：2026-10-26 ｜ 项目：RGSS3（RPG Maker VX Ace 运行库）重建
前置共识：`README.md` §一（202E 项目）+ 本轮 RGSS3 目标变更共识（Q1–Q14）

## 0. 目标与基线定义（锁定）

| 文件 | SHA-256（前 16） | 角色 |
|---|---|---|
| backup/RGSS300.dll（1,192,960 B，2012-01，带壳） | b9be93af b4d6be80 | **地面真值/行为基准**（汉化版：内置字符串中文化，用户背书） |
| backup/RGSS300_.dll（3,299,328 B，2024-04，脱壳+修改） | d0ca261d d5edb24f | 分析加速器 + 修改鉴定对象 |
| backup/RGSS301.dll（1,085,952 B，带壳，官方原版） | 27647690 ed16218c | 轻量 diff 参考（§4） |
| test/Project1 | — | 唯一测试游戏（原目录不动；System\RGSS300.dll 与 RGSS300_ 同哈希，即游戏平时跑的是修改版） |

重建产品 = 汉化版 RGSS300.dll 的行为（含汉化行为）。壳 = 也是 aspack（用户确认），与 202E 同族。

## 1. 脱壳与可运行件（复用 202E 流水线）

- **OEP = RVA 0x1812F4**，三重验证：① RGSS300_ 头部入口点；② 壳 stub 尾部同 202E 特征
  `ret 0xC; push <imm>; ret`（文件态 imm=0）；③ OEP 字节 = MSVC `DllMainCRTStartup`
  序言（`8bff 558bec 837d0c01 7505 …`）。
- `unpacker3.exe backup\RGSS300.dll unpacked\RGSS3_300_pristine 1812F4` → OEP 冻结命中，
  纯净 dump 0x329000 字节（SHA-256 ff46952254e0deff…f1cf05）。
- v1（进程内）会话补模块表（64 位 py 枚举 WOW64 冻结进程只见 7 个 64 位模块——
  与 202E 时相同的老坑）；`unpacker3` 管线一次通过。
- **apphelp 包裹槽 ×2 定名（双证据）**：反汇编调用点特征 + 与 202E 蹦床地址互证
  （同 boot 同地址 7014DEA0/701500B0）：
  - `0x1A236C = user32!IsWindowVisible`（单参；`neg/sbb/and 0x10000000` → WS_VISIBLE 位）
  - `0x1A2390 = user32!MessageBoxW`（4 参 hwnd/text/caption/uType；站点含全局消息缓冲
    0x10263068 + 格式化助手 0xC5F0）
- `mkpe2.py` 参数化改造（`--slot RVA=mod!Func` / `--self SUBSTR`，202E 旧默认行为不变）：
  31 组导入描述符、283 thunk（281 命名 + 2 槽覆盖），impcheck 全可解析；
  产物 `unpacked/RGSS3_300_probe.dll`（SHA-256 4c4c30e8663c7c49…405d6，入口 0x1812F4）。
- 冒烟：LoadLibrary @0x10000000 成功、27 导出可解析、卸载干净
  （smoke 报的 RGSSInitialize2/regex_new 缺失为预期——那是 202E 的导出名）。
- **游戏实跑**：`test/Project1_probe`（探针版）12 秒存活、窗口 "Project1"；
  `test/Project1_orig`（原版带壳基线）10 秒存活。RTP=RPGVXAce 已装
  （Steam `rtp`），无需额外处理。Project1 静态验证为原版兼容：
  无 Game.rgss3a、脚本无 resize_screen 实调用、无自定义 Decrypt 模块。

复现流水线：
```
tools\unpacker.exe backup\RGSS300.dll unpacked\RGSS3_300_v1        # 模块表
tools\unpacker3.exe backup\RGSS300.dll unpacked\RGSS3_300_pristine 1812F4
copy RGSS3_300_v1\{modules,modmaps,exports}.txt → RGSS3_300_pristine\
py tools\mkpe2.py unpacked\RGSS3_300_pristine unpacked\RGSS3_300_probe.dll 0x1812F4 ^
   --self RGSS3 --slot 1A236C=user32.dll!IsWindowVisible --slot 1A2390=user32.dll!MessageBoxW
tools\impcheck.exe … ; tools\smoke.exe … ; test\Project1_probe 实跑
```

## 2. 修改鉴定（RGSS300_ vs 纯净汉化版，全量字节级）

方法：两边均为 offset==RVA 布局，逐字节 diff 全镜像；.idata2 双方均为重建产物单独归类。
结果：**所有差异 100% 归因完毕**，无未解释字节。

- .data / .vxdata / .adata / 0x2B0000 段（0x15000）：**零差异**。
- .text：**仅 6 个簇、约 120 字节**（见下表）。
- .rdata：仅 IAT 区（0x1A2000–0x1A2566，绑定地址差异，重建产物）+ 1 处字符串。
- .rsrc：资源树键一致（13 项），内容差异见 §2.4。

### 2.1 修改 #1：分辨率上限 640×480 → 1024×768（×1.6）

| 类型 | 站点（RVA） | 原版 | 修改版 |
|---|---|---|---|
| push 对（窗口/resize 尺寸） | 0x19A5, 0x1A56, 0x1C523, 0x1F497, 0x10DFE8, 0x10E020, 0x10E054, 0x10E088 | push 0x1E0; push 0x280 | push 0x300; push 0x400 |
| push 对（外框尺寸变体） | 0x213DF | push 0x2A0; push 0x200 | push 0x420; push 0x320 |
| mov 立即数（最大宽高） | 0x10E7E7/0x10EFE9 (宽), 0x10E803/0x10EFF9 (高) | 0x280 / 0x1E0 | 0x400 / 0x300 |
| clamp 代码 | 0x20F6–0x2111 | cmp/jle/mov 上限 0x280、0x1E0 | 0x400、0x300 |
| 次要边界（同 ×1.6 取整） | 0x1F5D, 0x1FE1 | 0x10(16)、0x15(21) | 0x19(25)、0x21(33) |

### 2.2 修改 #2：F12 重置屏蔽

- 函数 **0x1EB0** = F12 轮询：`GetKeyState(0x7B=VK_F12)`（IAT 槽 0x1A2388）→
  若按下 → `call 0x26210`（= rb_raise，M2 已还原；对全局 0x10261BC4 =
  RGSSReset 类抛出）。修改版将其 **4 个调用点全部 NOP**：
  0x1FC0、0x2828、0x2C00、0xB014（各 5 字节）。
- Run(0x10001620) 内 sub_10019B70 检查块（0x1939–0x1963，音频就绪/字符串检查失败时
  载入错误串 207 并弹框返回）：0x1937 `jne→jmp` 无条件跳过 + 0x195F nop→
  `jmp 0x1C6B`（cookie 尾强制返回）双保险。（M2 复核修正：此块为错误路径而非
  F12 分支本身，M1 初判"重置恢复路径"系近邻误读。）

### 2.3 修改 #3：默认字体 Font.default_name

- .rdata **0x1A7B34**：紧跟 Ruby 级符号 `@@default_name`（Font 类变量默认值，
  代码引用点 0x998B）：`"nsimsun"` → `"simhei"`（原地等长替换）。
- 注：官方原版该值应为 `"VL Gothic"`（由 301/300_ 的资源串 "VL Gothic フォントが見つかりません"
  佐证）；汉化版改为 nsimsun，修改版再改为 simhei。

### 2.4 用户嫌疑四项的核对结论

| 嫌疑（用户 Q4/Q13） | 结论 |
|---|---|
| 默认字体 | ✅ 实锤，§2.3 |
| F12 屏蔽 | ✅ 实锤，§2.2 |
| 分辨率限制 | ✅ 实锤，§2.1 |
| **解密函数** | ❌ **证伪**：全镜像字节级 diff 无任何解密/加密相关代码或表修改；.vxdata（RGSS 归档数据区）零差异 |

### 2.5 血统修正（重要）

RGSS300_ 的 .rsrc 资源 = **官方日文+英文**（串表 13/14 英文、19/20 日文；对话框 6 个），
而汉化版 = 中文串表。⇒ **RGSS300_ 并非从这份汉化版脱壳**，而是源自官方原版
（用户 Q13 的目视判断在资源层面被推翻；.text 99.99% 一致是因为汉化只动了 .rsrc）。
对项目无碍：重建基准仍是汉化版（Q14 决议），RGSS300_ 仅作 .text 分析加速器
（其 80 字节差异已全部归因，可安全使用）。

## 3. 关键鉴定事实

| 项 | 结论 | 证据 |
|---|---|---|
| Ruby 版本 | **1.9.2p0**（2010-08-18，YARV/M17N/Onigmo 时代） | dump 内 `ruby 1.9.2p0 (2010…` 串；300 与 301 相同 |
| 官方源码 | ruby-1.9.2-p0.tar.gz（10,787,899 B，SHA-256 8c0c4e261a921b5c406bf9e76ac23bf3c915651534e9d1b9e8c5d0bee4a7285c）已入 third_party/ | cache.ruby-lang.org |
| 导出 | **27 个**（RGSSInitialize3 起，无 regex_* 家族） | exports.txt，与带壳原版逐名一致 |
| 导入 | 283 thunk / 31 组：kernel32×154、user32×64（62+2 槽）、ws2_32×17、gdi32×12、winmm×11、advapi32×9（含 Crypt*）、msacm32×7、ntdll×4、shell32×3、combase×2 | mkpe2 + modmaps |
| 与 202E 导入对比的要点 | 全面 A→W 化（lstr*/GetModuleFileName/环境变量等）；ws2_32 8→17（Ruby 1.9 socket）；新增 CryptAcquireContext/GenRandom、SHGetSpecialFolder 系、控制台输入 API | 双向差集清单 |
| 图形/音频 | 纯 GDI + winmm(MCI/mmio) + msacm32，无 DirectX（资源串里的 "DirectX 8.0" 是历史遗留错误消息文案） | 导入表 |
| 壳 | 与 202E 同族 aspack（.vxdata/.adata/.idata2/.aspack 节 + 相同 stub 尾特征） | §1 |

## 4. 300 ↔ 301 轻量差异清单（Q9a）

- 301 ImageBase 0x10000000、OEP **0x316864**（≠300 的 0x1812F4；CRT 入口模式扫描唯一候选
  0x181164 反而未被执行）。301 的壳为**双阶段**（stub→0x316864 中转→真 OEP），
  且疑似**清 DR 寄存器反调试**（硬件断点 0x181164 不触发）。
  `unpacker3` 目前只能冻结在中转站（unpacked/RGSS3_301_pristine/dump_midstage_invalid.bin，
  不可用，见 NOTE.txt）；301 可用分析镜像 = `RGSS3_301_v1/dump.bin`（.text/.rdata 有效）。
- **字节级 diff：.text 约 1.6MB/1.6MB 差异（≈全量），.rdata 182 页差** ⇒
  300 与 301 是**同源不同编译的完整重编译版本**（同为 ruby 1.9.2p0，
  `@@default_name`/`RGSSReset` 等地标串两侧俱全）。
- 结论：**301 不能作字节级参照**；函数级对应（按符号+字节匹配，用 REA/IDA）移交 M2。
  301 若需可运行纯净件，需给 unpacker3 加 INT3 链式追 OEP（按需再做）。

## 5. 产物清单

```
unpacked/
├── RGSS3_300_pristine/     OEP 纯净 dump + 模块/导出映射（★地面真源）
├── RGSS3_300_v1/           v1 进程内 dump（.data 脏，模块表来源）+ trampfix
├── RGSS3_300_probe.dll     可运行重建 PE（283 导入全解析，已实跑游戏 12s）
├── RGSS3_300_modded_ref.dll 修改版入库副本（= backup/RGSS300_.dll）
├── RGSS3_301_v1/           301 分析镜像（.text/.rdata 有效）
└── RGSS3_301_pristine/     ⚠ 中间态废件 + NOTE.txt（301 脱壳待 INT3 方案）
test/
├── Project1_orig/          基线副本（System\RGSS300.dll = 带壳汉化版）✅ 实跑
└── Project1_probe/         探针副本（System\RGSS300.dll = RGSS3_300_probe.dll）✅ 实跑
tools/mkpe2.py             已参数化（--slot/--self），202E 行为不变
third_party/ruby-1.9.2-p0/  官方源码树
```

## 6. 遗留与 M2 入口

1. 0x19B70(0x101A255C, …) 的重置判定语义（RGSSReset 挂起检查？）——M2 反编译还原；
   0x101A255C 指向非字符串字节（疑似 Ruby ID/VALUE）。
2. RGSS300_ 修改版若需行为级复验（如 simhei 在无该字体机器上的表现），可在
   Project1_probe 上做动态对照——非阻塞。
3. 301 函数级对应清单（Q9a 后续）；301 可运行件（INT3 链）按需。
4. M2 主体沿用 202E 方法论：RTTI 扫描枚举 C++ 类 → 27 导出签名/语义 →
   模块地图 → ws2_32×17 / Crypt* / shell32 / 控制台 API 用途鉴定。
5. F1 属性对话框（6 个 RT_DIALOG）与错误消息资源 = 汉化行为的一部分，
   重建产品需复刻汉化版资源（中文串表）。
