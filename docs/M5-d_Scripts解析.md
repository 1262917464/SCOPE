# M5-d — Scripts.rvdata 完整解析（研究资产）

日期：2026-10-26 ｜ 工具：`tools/parse_rvdata.py`（自研 Marshal 4.8 解析器）

## 1. 格式完全解出

```
Scripts.rvdata
  ├─ 前 2 字节: Marshal 版本 = 4.8
  └─ 顶层: Array(95)
       每项: [ section_id(Integer), name(String), zlib_data(String) ]
```

**关键验证**：磁盘上每项只有 **3 个元素**。运行时引导脚本
`$RGSS_SCRIPTS.each { |s| s[3,0] = Zlib::Inflate.inflate(s[2]) }` 动态写入索引 3
（`s[3,0] = x` 是 Ruby 1.8 的"在索引 3 处插入"，把数组从 3 元素扩成 4 元素）。
这与 M5-a 主循环反编译结果**逐字吻合**，交叉验证了 zlib 用途与脚本装载机制。

## 2. 解析结果

- 95 个脚本段，**zlib 解压全部成功（0 失败）**
- 解压后总计 **613,115 字节 Ruby 源码**
- **1,227 个 `def` 方法定义**
- 归档位置：`docs/assets/Project3_scripts/`（95 个 .rb 文件，按段序命名）
- 本项目测试游戏 Project3 为**繁体中文版**（脚本注释即中文）

## 3. 段结构（前 20 段）

| # | 名称 | 源码字节 | # | 名称 | 源码字节 |
|---|------|---------|---|------|---------|
| 000 | (空/分隔) | 0 | 010 | Game_Switches | 1,384 |
| 001 | ▼ 模組 | 0 | 011 | Game_Variables | 1,402 |
| 002 | Vocab | 5,524 | 012 | Game_SelfSwitches | 1,282 |
| 003 | Sound | 2,101 | 013 | Game_Screen | 8,020 |
| 004 | Cache | 3,906 | 014 | Game_Picture | 5,751 |
| 005 | (空) | 0 | 015 | Game_Battler | 44,191 |
| 006 | ▼ 遊戲物件 | 0 | 016 | Game_BattleAction | 13,915 |
| 007 | Game_Temp | 2,373 | 017 | Game_Actor | 27,256 |
| 008 | Game_System | 3,247 | 018 | Game_Enemy | 10,770 |
| 009 | Game_Message | 2,985 | … | … | … |
| | | | 093 | Main | ~2,000 |
| | | | 092 | ▼ 主程式 | 0 |

（`▼` 开头的段是 VX 脚本编辑器的分组标题，内容为空）

## 4. 研究价值

1. **完整游戏逻辑参考**：Project3 的 1,227 个方法展示 RGSS API 的**实际使用方式**
   —— 这是重建实现"行为正确"的最直接依据（比只读反汇编更实用）。
2. **API 覆盖率验证**：可从脚本反推哪些 RGSS 方法被真实调用、调用参数范围，
   用于 M6 测试用例设计。
3. **汉化基线**：中文注释与 `Vocab` 模块直接展示文本管线使用方式，
   对"可打补丁基线"目标有直接价值。
4. **格式知识**：`Scripts.rvdata` 结构 + 3→4 元素填充机制已完全掌握，
   可写任意 rvdata 读写工具。

## 5. 附：工具能力

`tools/parse_rvdata.py` 现可：
- 解析 Marshal 4.8 全类型（nil/bool/int/symbol/string/array/hash/object/userdef/ivar/float/struct/class）
- 提取并 zlib 解压任意 Scripts.rvdata
- 输出按段编号的独立 .rb 文件

同一解析器可直接用于 `Map*.rvdata` / `Actors.rvdata` 等所有 VX 数据文件
（结构同为 Marshal）。
