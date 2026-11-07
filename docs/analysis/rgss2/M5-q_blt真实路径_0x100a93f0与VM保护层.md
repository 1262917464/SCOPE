# M5-q blt 真实执行路径逆向笔记（0x100a93f0 + 运行时模板拼装）

> 2026-11-07，review+整修线。Frida 动态白盒取证 + 静态剥离。
> 本文更正 M5 前期"混合器 0x100a43e0 家族"的定性——那是另一表面格式的路径。

## 一、动态取证（Frida，evidence: frida_v2_evidence.json / session 59892）

Game.exe 跑 blt 密集矩阵，hook 分流点 0x1009685a：

- **全部 blt 走 `jne 0x1009685a` 分支**（0x1009670c 的 `cmp WORD [dst+0xe],8` 不成立
  ——测试表面 +0xe = 0x5641 的高半段，非"8bit alpha"类）→ 558 次命中
- mode(ecx) = **0x80000830** 确认
- `[pixbuf+0x28]` = **运行时堆 traits**（0x3348e00/0x336ba20/…每次调用不同！
  非静态 0x1015e710——那是 +0x24 槽存的）→ 表 +4 槽 = **0x100a93f0**

**结论：真实 Bitmap blt/stretch_blt 的像素工作全部在 0x100a93f0。**

## 二、0x100a93f0 主体结构（静态逐指令）

- 入口检查 src 格式 `[ebx+8]+0xe == 0x20`（32bpp），否则 return 0
- **mode&0x10 = "带 opacity"标志**：置位时 opacity 取 mode 结构 **+0x34**（必须 ≤0xFF，
  否则拒绝）——更正"opacity 缺省 0xFF"的旧描述：0xFF 只是标志未置位时的缺省
- 矩形处理：|dw|≤|sw|/2 类缩放检查 + mode&0x2000 滤波位 + 双侧收缩 1px
  （0x100a9549-0x100a95a9，与 0x100a3f10 同款逻辑内联）
- 调 0x10095f10（相交）/ 0x100960f0（步长计算），结果存 [esp+0x78] 标志
- **内循环二选一（0x100a96cd）**：`[esp+0x78]==0 → 0x100a8be0；≠0 → 0x100a8e80`
  选择结果缓存在 dst pixbuf 对象 +0x20/+0x24/+0x28
- 调用内循环时传 **19 个参数**，包括 opacity LUT 指针
  （0x100a9723: `shl edx,8; add 0x1019ca90`）——LUT 机制在此路径同样确认

## 三、内循环 0x100a8be0 = 运行时模板拼装器（2026-11-07 更正："VM"定性错误）

0x100de190 = **memcpy**（218 个调用者，rep movs 特征）。0x100a8610 = **跳转发射器**：
`emit(ctx, target, val, 0x75, 0x0f85)` —— delta∈[-127,127] 发射 `0x75(JNE rel8)`，
否则 `0x0f85(JNE rel32)`。0x100a8660 同族（含直接内联的 MMX 模板块 0x100a8677：
权重表 0x1018bb90 + psubw/pmullw/psrlw/paddb）。

即 0x100a8be0 = **按 mode 从预制 MMX 模板块（0x100a8beb 等，本身就是合法 x86）
用 memcpy 拼装专用循环 + 0x100a8610 发射分支**——代码特化（code specialization），
不是字节码 VM，不是反逆向。静态 dump 里看到的"支离破碎"正是模板块原样。

- 残留的真实指令：mode&0x800 分支读 `[esi]`（src 像素）+`[esi+3]`（srcA）
  → `movzx eax,[edx+eax]`（**LUT 查表**）；mode&0x800==0 分支用 `[esp+0x34]`
  （**固定 alpha 参数**，对应"整体 opacity 代替逐像素 alpha"的模式）
- 正向/反向像素推进（`add esi,4/add edi,4` vs `sub esi,4/add edi,4`，
  mode 位选择）
- 行推进 `add esi,[esp+0x24]; add edi,[esp+0x20]; dec [esp+0x30]`
- 模板块（0x100a8beb/0x100a8c7f/0x100a8cfd/0x100a8cc4…）：真实 MMX 混合指令，
  按 mode 位选择串入：mode&0x800 → LUT[srcA] 查表；mode&0x800==0 → 固定 alpha
  参数 [esp+0x34]；0x100a8cc4 = `movzx eax,[edx+eax]` 即 LUT 查表指令
- 发射的 JNE 把块按 mode 连成专用循环，执行后经 0x100a8dc5/0x100a8e1d 的
  发射调用收尾

## 四、对 B33/B50 现状的影响

- 已逐位吻合部分：da=255 的移位形式（0x100a0600 链，另一格式家族，数学等价）、
  全部 alpha 值（G/G2 全扫描 510 点 alpha 全对）、da=0/ea=0 直通、点采样映射
- 未吻合：中间 da 的 RGB 非线性（G 行 149/255、G2 行 142/255 点 ±1）
  ——根源在 0x100a93f0 路径的 VM 字节码实现，公式级复刻不可行，
  必须逆 VM（0x100a8610 解释器 + 字节码语义）或做指令级 trace
- 黑盒数据禁止用于拟合（用户指令）；现存实现 = 已确证指令语义的忠实转写，
  残差如实标注

## 五、下一步（若继续）——2026-11-07 更正后

1. ~~逆解释器~~ → **直接 dump 拼装产物**：Frida 在 0x100a8be0 返回前
   把 [ebp+8] 起始的生成代码 memcpy 出来反汇编——即当前 mode 的真实循环机器码
2. 对比 A 行（da=255）与 G 行（da=64）的生成代码差异 = 模板选择规则
3. 按生成代码语义重写 C 内循环（模板块已部分可读：LUT 查表、借补、合成链）
执行流证据：Frida exec hook 确认 0x100a93f0→0x100a8be0（1:1）/0x100a8e80
（缩放，arg6=4）真实执行，mode=0x80000830，src/dst pixbuf 参数吻合。

## 六、终局（2026-11-07，B33/B50 完全销账）

Frida dump 生成代码成功（codegen_A_da255.bin，147 字节，Stalker 不可用后改
在 0x100a8e70 锚点读 [ebp+8] 终值差）。逐指令反汇编得到最终语义：

```
ea    = LUT_op[srcA]                    # 0x1019ca90 + op<<8 块
wi    = T1[(dstA<<8) | ea]              # 0x1018ca90, 64K 权重索引表
out_a = T2[(dstA<<8) | ea]              # 0x101aca90, 64K alpha 表
W     = W8[wi] = (wi+1, wi+1, wi+1, 0)  # 0x1018bb90；W8[0] = 全 0
out_c = dst_c + sar8((src_c - dst_c) * W)   # psubw/pmullw/psrlw/paddb 回绕链
da==0 特例: out = (src_rgb, ea)
```

**关键领悟**：之前四族数学模型全部矛盾的根源——T1/T2 是 64K 预计算查找表，
每个 (dstA, ea) 对一个独立值，**根本不存在公式**。黑盒数据禁拟合（用户指令）
反而逼出了正道：dump 生成代码 → 发现查表 → 照抄表。

实现：blt_tables.h（T1/T2 各 64KB 照抄）+ blt_compose 查表化 + W8[0] 特判。
验证：Game.exe 端到端 52 行全矩阵（A/B/C/D/E/E2/E3/F/G/G2，含 da 1..255
全扫描 510 点，共 800+ 数据点）**与原版逐位一致**；b19test 回归 ALL PASS。

教训：
1. "数学公式不存在时，表就是答案"——对拟合失败要保持怀疑表的存在
2. Frida dump 生成代码 > 逆拼装器 > Stalker（本环境不可用）> 数值拟合
3. Memory.readByteArray/Stalker 等新 Frida API 变动需逐个验证
4. 门控测试（go.txt 文件）彻底解决 attach 竞态
