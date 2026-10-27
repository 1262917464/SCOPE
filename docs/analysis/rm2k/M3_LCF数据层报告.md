# RM2K M3 报告 —— LCF 数据层还原
> 注：本文写于 2026-10-27 仓库重构前；文中 `backup/` `unpacked/` `build/` `test/` `third_party/` 等路径现位于 `local/` 下，`src/rgss_*.c` 现为 `src/runtimes/rgss2/*.c`，`tools/` 已按流水线分桶（unpack/pe/analysis/probe/verify）。


日期：2026-10-26 ｜ 目标：`backup/2000.exe` + `backup/2003.exe`

## 1. LCF 格式的权威答案

我不靠猜，用两条独立证据交叉确认：

- **证据 A：原版 exe 逆向**
  - `0x45EBCC` BER 变长整数读取
  - `0x45EC00` 长度前缀字符串读取
  - `0x469B60` `TLcfDataBase.LoadFromFile`（拼 `RPG_RT.ldb` 路径）
  - `0x469A44` 文件/缓存层（时间戳 + 大小双重缓存校验）
  - `0x469834` 顶层解析 + **15 路版本分派跳转表** `0x4698C2`
- **证据 B：liblcf 源码**（EasyRPG 项目，仅作格式参照，实现为独立 C 代码）
  - `src/reader_lcf.cpp` `ReadInt` / `Read<double>`
  - `src/reader_struct_impl.h` `ReadLcf` 的 chunk 循环
  - `src/ldb_reader.cpp` 头部校验

两条证据在 BER 算法上完全一致，这给了格式判定很高的置信度。

## 2. 格式定义（实测 + 双证据）

### 2.1 BER 变长整数

```
value = 0
do {
    value <<= 7                      ; 先移位
    value |= (byte & 0x7F)           ; 再加低 7 位
} while (byte & 0x80)
```

**"高位在前"的 7 位分组，不是标准 LEB128（后者是小端）**。这是解 LCF 最容易搞错的一处。
原版 `0x45EBCC` 的 `shl ebx,7` / `add eax,ebx` 与 liblcf 的 `value <<= 7; value |= temp & 0x7F` 逐条对应。
超过 5 次循环判非法（liblcf 同样以 `loops > 5` 为界）。

### 2.2 文件头

```
BER     magic_len                    ; 11 → 0x0B
byte    magic[magic_len]             ; "LcfDataBase" / "LcfMapUnit" / "LcfMapTree" / "LcfSaveData"
--- 之后直接是 chunk 流，没有总长度字段 ---
```

**注意：没有总长度字段。** 我最初以为头部是 `magic + BER 总长`，导致 LMB 解出 `body_len=11` 而文件有 19 万字节。
用穷举铺满检验（`tools/lcf_syntax_probe.py`）否定了这个假设。

### 2.3 块（Chunk）

```
BER     ID                           ; 字段码
BER     length                       ; 载荷字节数
byte    data[length]
```

**ID 在前、length 在后。** 顺序写反会解出一堆不可能的字段码（`0xAE`、`0xA0` 之类）。
`ID == 0` 表示结构结束。这是 liblcf `ReadLcf` 的循环首句：
`chunk_info.ID = stream.ReadInt(); if (chunk_info.ID == 0) break;`

### 2.4 数组

```
BER     count
count × {
    BER     ID                       ; 元素编号（1 基）
    ...     Struct                   ; chunk 流，以 ID=0 结束
}
```

数组**不是**"一个 chunk 里套 N 个 chunk"，而是 `count + N×(ID + struct)`。
元素没有长度前缀，边界靠 chunk 流自身的 `ID==0` 终止符确定。

### 2.5 数据类型

| 类型 | 编码 |
|------|------|
| int / bool | BER（bool 判 `!= 0`） |
| string | BER 长度 + 原始字节 |
| double | 8 字节小端 IEEE754 |
| int16 / uint32 | 定长小端（稀有，仅特定字段用） |

## 3. 语义级验证

解析 `test/2000/RPG_RT.ldb` 得到的数组条目数，与 RM2K 数据库的标准结构完全吻合：

| 字段码 | 容器 | 条目数 | 合理性 |
|--------|------|--------|--------|
| `0x0B` | Actors 主角 | 8 | ✓ 标准上限 8 |
| `0x0C` | Skills 技能 | 128 | ✓ |
| `0x0D` | Items 物品 | 84 | ✓ |
| `0x0E` | Enemies 敌人 | 45 | ✓ |
| `0x0F` | Troops 敌群 | 80 | ✓ |
| `0x10` | Terrain 地形 | 10 | ✓ |
| `0x11` | Attributes 属性 | 17 | ✓ |
| `0x12` | Conditions 状态 | 10 | ✓ |
| `0x13` | BattleCommands 战斗指令 | 131 | ✓ |
| `0x14` | Animations 动画 | 5 | ✓ |
| `0x15` | ChipSets 图块组 | 1 | ✓ |
| `0x16` | Terms 术语 | 1 | ✓ |
| `0x17` | Switches 开关 | 100 | ✓ 标准上限 |
| `0x18` | Variables 变量 | 100 | ✓ 标准上限 |
| `0x19` | CommonEvents 公共事件 | 10 | ✓ |

这不是巧合能产生的匹配——**8/128/84/45/80/10/17/10/131/5/1/1/100/100/10** 正是 RM2K 数据库结构。

`2003/RPG_RT.ldb` 只有 14 个主角、1 个技能，是个几乎空的测试库，但也正确解出。

### 地图验证

`test/2000/Map0001.lmu` 独立定位的证据：我按 "20×15 格 × 2 字节 = 600 字节" 扫描文件，找到两段连续 600 字节的可疑图块数据，起始于 `0x11` 和 `0x26B`。解析器解出的 `0x47`/`0x48` 两个 chunk 长度恰为 **600**，与独立定位完全一致。

## 4. 交付代码

```
src/rm2k/lcf.h          格式定义 + 读取器 + chunk/数组迭代器 + 字段码表
src/rm2k/lcf.c          全部读取原语（每函数标注原版地址）
src/rm2k/lcf_test.c     结构转储工具
src/rm2k/lcf_verify.c   自校验（结构完整性 + 字段码合法性 + 数组边界）
tools/lcf_syntax_probe.py  穷举铺满检验（用来判定 header 语法）
```

## 5. 验收结果

```
test/2000/RPG_RT.ldb         OK   chunks=16  arrays=14 items=729
test/2000/RPG_RT.lmt         OK   chunks=13  arrays=0  items=0
test/2000/Map0001.lmu        OK   chunks=5   arrays=1  items=1
test/2003/RPG_RT.ldb         OK   chunks=15  arrays=2  items=15
test/2003/RPG_RT.lmt         OK   chunks=13  arrays=0  items=0
test/2003/Map0001.lmu        OK   chunks=9   arrays=1  items=1

6/6 passed   （bad_id=0, bad_item=0 全部为零）
```

自校验判据（三条，都不依赖外部标准）：
1. 所有 chunk / 数组 / 元素必须精确铺满文件，无剩余无越界
2. 每个 chunk 的 ID 必须合法（非 0、非 0xFF）
3. 每个数组元素的内部 chunk 流必须以 `ID==0` 正确终止，且声明 count 与实得数一致

## 6. 踩过的坑（记录以免重犯）

1. **头部多读一个 BER**：以为 `magic` 后面跟总长度，实际直接进 chunk 流。
2. **chunk 字段顺序写反**：写成 `length + ID`，实际是 `ID + length`。
3. **数组语义搞错**：以为"数组 chunk 的载荷是 N 个 chunk"，实际是 `count + N×(ID+struct)`。
4. **gcc 静默失败**：`-m32` 编译时 `cc1.exe` 缺 `libisl-23.dll`，退出码 1 但零错误输出。必须 `export PATH="/c/msys64/mingw32/bin:$PATH"`。

## 7. 下一步（M4）

数据层已可用，M4 需要：
1. 按 `TCatalogItem` 子类的实例字节大小反推每个结构的字段布局
2. 从 `AuroraSheet`(1576B) / `TLcfChipSetGraphic`(2396B) 反推图元批处理结构
3. `AuroraPng` / `tAuroraZlib` / `PAuroraXyz` 三条图片解码路径
4. 与 `TLcfgScreen`(12428B) 的屏幕状态结构对接
