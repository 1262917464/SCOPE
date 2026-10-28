# M5-n 第六轮 — 机制全部闭环，仅剩参数块字段记账

## 决定性收尾发现
1. **0x100ae760**（threshold→idx）: 若 param[0]&0x10 → idx = param[+0x34]
   （= 块 f13 = 每帧推进值，>0xFF 时该层不绘制）；否则 idx=0xff。
   → 文件转场进度就存在参数块 f13，每帧由环写入。
2. **0x100a5710 尾部**：两次终绘调用 —— param[0]&0x800 → 0x1009ffb0，
   否则 **0x1009fe00** —— 正是 M5-k 解码过的 draw_text MMX 混合器
   （F 表 0x1018bb90）！9 参数含 (dst=帧, src=冻结图, mask 图, 跨距×2,
   w, h, **因子 LUT**)。即文件转场的逐像素混合与 draw_text 完全同一函数，
   因子来源从字体 alpha 换成 lut[mask_byte]。
3. LUT 构造（0x100a5710 主路径）:
   - idx = f13（进度 0..255）
   - lut[256]: 渐变带（宽 = param[+4] = vague）随 idx 移动；带内
     v = (i−t0)*255/vague，lut[i] = LUT2[idx*256 + ((0xff−v) ^ phase)]
     （LUT2 = 0x1019ca90）；带外 memset 端点；phase = (param[0]>0x100)?0xff:0。
4. 默认 crossfade（无文件）也走 0x1009fe00 同一族（子内核[0] 0x100a1d90 已解码，
   纯色+mask 版）。

## 唯一未定（下一轮 30 分钟）
文件环（0x10008238）参数块 14 字段的**最终布局**（f0 是否 0x1000、f13 写入点）。
要点：循环体内 `mov [esp+0x2c],eax` 在两次 push 之后执行（esp=E−8），
可能落在 f2 槽；需按 E（环外 esp）逐槽重排。constraint：
- kernel 要求 f0<=0x200 且 f2==0x100 才走普通分支 → 文件路径 f0 必 ≤0x200
  且 f2≠0x100 → f0 ≠ 0x1000（0x1000 是别的槽）。
- kernel idx = f13 ≤ 0xFF → f13 = 每帧进度（文件环某槽每帧 +0x100/dur，
  存储尺度可能 /2 或 256 系）。

## 实装就绪度
全部算法组件已指令级解码：
  lut 构造（渐变带+phase）→ lut[mask] → 0x1009fe00(F 表混合)。
剩余只是把文件环的槽位对齐后照抄。实装 + 自制遮罩对拍即可完成 M5-n。


# M5-n 第五轮 — 文件转场内核 0x100a5710 主路径解码（机制闭环）

## dispatch 修正
文件环参数块基址有 3 种对齐可能，但 kernel 0x100a5710 的入口检查给出约束：
- `cmp [param+8],0x100`：==0x100 → 普通路径（再按 param[0]&0x800 分
  0x100a43e0 / 0x100a53a0）
- !=0x100 → 文件主路径，先校验 param[0]<=0x200、param[+4]<=0x1ff、bpp==32、
  param[+8]%0x200!=0 → 进入遮罩主流程。
（param 基址的最终确认：以 param[+8]==f2 与 f4=threshold、f5=vague 的写入
  对齐为准 —— 文件环写 [esp+0x1c]=0x1000 与 [esp+0x24]=0x100、[esp+0x28]=vague、
  [esp+0x2c]=threshold，与 kernel 的 f2/f4/f5 引用相容的对齐 = 基址 [esp+0x1c]：
  f0=0x1000, f2=0x100, f3=vague, f4=threshold, f5=0, f6=edx）

## 文件主路径逐指令语义（0x100a5710 续）
1. `call 0x100ae760(param, &idx)` → idx=[ebp-0x174]（**当前帧 threshold 映射索引**）
2. phase = (param[0] > 0x100) ? 0xff : 0x00   （[ebp-0x115]，反相标志）
3. 构造 256B 因子表 lut[ebp-0x110]：
   - 段1 [0, t0): memset 风格填 LUT2 常量端点
     (0x100dc6e0 memset, 值 = 0x1019ca90[idx*256 + (0xff^phase)])
   - 段2 渐变带 [t0, t1)（宽度 = param[+4] = vague，[ebp-0x11c]）:
     v = (i − t0)*0xff / vague ; lut[i] = LUT2[idx*256 + ((0xff−v) ^ phase)]
     —— **vague 宽度的线性过渡带，随 threshold 移动**
   - 段3 尾部: memset 另一端点 (0x100a5aa5)
   （LUT2 = 0x1019ca90，按 idx*256+v 查；idx 即当前帧推进量）
4. 随后进入逐像素循环（0x100a5ae0 起：0x1009fdf0/0x10003f10 取位图指针/跨距，
   imul [ebp-0x16c] 行偏移）—— 遮罩字节查 lut → 因子 → 混合。

## 结论（文件转场完整算法骨架）
```
每帧: threshold_idx = f(i)            # 文件环: threshold += 0x100/dur (0x100..0x200)
      lut[256]: 渐变带中心/边界随 threshold_idx 移动, 带宽=vague, 端点由 phase 反相
      每像素: factor = lut[mask_byte]
              out = dst + (src − dst) × F[factor] >> 8     # F 表同 draw_text
```
- phase(0xff/0) 对应 RGSS 文件转场的方向语义（亮→先 或 暗→先）由 param[0] 判。
- threshold 的 0x100..0x200 步进与 LUT2(0x1019ca90, alpha<<8 索引) 的关系已定位
  （0x100ae760 为映射函数，待最后一读）。

## 下一轮（实装前置，2 小项）
1. 读 0x100ae760（threshold→idx 映射）+ 逐像素循环尾部的混合调用（确认用的是
   哪个 F 表/方向）。
2. 实装: 文件环参数 → 每帧重建 lut → 遮罩查表 → F 表混合。对拍: 自制灰度
   遮罩（左黑右白+渐变带）跑 orig/rebuild difftest。


# M5-n 第四轮 — 转场混合内核已解码（决定性突破）

## 完整调用链（全部指令级确证）
```
Graphics.transition(文件)                    0x10007fb0
  └ 文件环 0x10008238: 每帧 0x1009a280(+0x258层, 1, &param{f2=0x1000,f4=0x100,f5=vague,f6=threshold↑})
     └ 层合成 0x10014afe: 拷参数块×2 → blit
        └ 0x10096680 blit 分发器: flags=块[0](0x810) → 0x800 位 → [img+0x24] 方法表
           (双变体: 0x1015e70c→vtable 0x1014051c / 0x1015e710→vtable 0x101404b4;
            0x810&0x800 → [+0x30] = entry12: 0x101404b4 表 = 0x100a4700)
           └ 转场内核 0x100a4700:
              * esi = param[+8]>>24 (全局 alpha), =0 则整帧跳过
              * esi<<8 + 0x1019ca90 → 按 alpha 索引的 256 项乘法 LUT 基址
              * ebp = param[0]  → ebp&7 → 子内核表 0x10143328[0..7]:
                [0]0x100a1d90 [1]0x100a1e60 [2]0x100a1f10 [3]0x100a1fd0
                [4]0x100a20f0 [5]0x100a2220 [6]0x100a2310 [7]0x100a1000
              * 0x810&7=0 → 子内核[0]
```

## 子内核[0] 0x100a1d90 逐指令公式（MMX）
参数: dst 指针/跨距、mask 字节图指针/跨距、w、h、纯色 color、**mask→factor 256B LUT 基址(ebx)**
```
for each pixel:
  a = lut[mask_byte]                      ; 遮罩 → 实际混合因子 (0..255)
  F = Ftable[a]  (@0x1018bb90, F[a]=a+1;  与 draw_text 同一张表!)
  out = dst + ((color − dst) × F) >> 8    ; 四通道 MMX, packuswb 钳制
```
- 混合公式与文字渲染完全同族（dst + (src−dst)×F>>8）。
- 遮罩字节经 **256B 查表** 变成因子 —— threshold/vague 就编码在这张 LUT 里
  （threshold 每帧递增 → LUT 每帧重建或平移）。
- 子内核[0] 是"遮罩×纯色"；冻结图↔实况图的图像对图像混合在子内核[1..6]
  （0x100a1e60/0x100a1f10/0x100a1fd0/0x100a20f0/0x100a2220/0x100a2310，待解码）。

## 剩余（下一轮，范围已很小）
1. 解码 mask→factor LUT 的构造（threshold/vague → 256B 表；在文件环或
   0x10014afe 附近找 256 字节写循环）。
2. 解码子内核[1..6]中图像对图像的变体，确认文件转场用哪个。
3. 实装 + 自制灰度遮罩对拍。

## 附: 其它确证
- alpha 权重 2D LUT 基址 0x1019ca90（alpha<<8 索引）。
- 0x100a4700 之前的分发链条全静态可复现，动态 frida 证据见
  frida_evidence9/13.json（ret=0x82dc 转场环 20 帧、窗口层 0x80000050 块等）。


# M5-n 补充 — blit 分发器与内核家族（第三轮，已闭合到内核候选）

## 0x10096680 blit 分发器（逐指令）
签名: 0x10096680(this=图像对象, dst_rect*, src_rect*, ?, flags*)
- this+0x8 → DIB 信息 (+0xe = bpp, 8/32 两分支)
- this+0x24 = 方法表指针(0x1015e70c → vtable 0x1014051c, 32bpp 变体)
- flags 解析（按优先级）:
  * 0x4000/0x8000 = Y/X 翻转（交换 rect 分量）
  * (fl&0xc0)==0xc0 且 (fl&0x820)==0x820 → [vtable+0x38]
  * 0x20000 → [+0x20]；0x800 → [+0x30]；0x20 → [+0x34]；0x200 → [+0x24/0x2c]
  * 0x1000 → [+0xc]；否则 [(fl&0xc0)==0xc0 ? 走 0x20/0x34 链 : +0x1c/0x10…]
  * fl<0（符号位）→ [+0x4]（镜像路径）
- 32bpp 方法表 0x1014051c 内容:
  [0]=0x100a8330(双线性重采样, 0x10000 定点+边缘钳制——缩放用)
  [1]=0x100a7fa0(矩形 blit 入口, bpp 分支)
  [2]=0x100a8070(带裁剪 blit → 0x10095f10 裁剪 → 0x100a7db0 复制核心)
  [4]=0x100a82c0
  [10]=0x100a7d10(纯色 alpha 填充: param[+8]&0xff → 0x101bd290 表 → memset 行)
  [11]=0x100a7f50(可绘制性检查: param[+8]>=0xff000000 直接拒绝)
  其余=stub 0x100a7f90
- 底层原语: 0x100a7c20(mask 填充: color×掩码AND写入)、0x100a7c90(8bpp 版)、
  0x100a7db0(行复制/翻转)

## 仍待咬死
- 转场层 blit 实际传入的 flags 值与命中的内核。转场块 f0=0x810 含 0x800 位,
  按上表 [+0x30]=stub —— 说明要么存在其它 bpp 变体方法表(ctor 0x10095cb0 的
  非 8bpp 分支可能再分), 要么 flags 在 0x10014afe 内被改写后才传入。
  候选内核: 0x100a82c0(纯色混合?) / 0x100a8070 / 0x100a7fa0 家族 +
  0x100a93d0(image inner vtable 0x10140504[0])。
- 定位法: frida 钩 0x10096680 入口读 [esp+0x38] 的 flags 值 + [ecx] 虚表地址,
  一次转场即可定案; 再对命中的内核逐指令解码 threshold/vague。


# M5-n 工作笔记 — 文件转场遮罩解码（进行中·第二轮）

## 本轮决定性进展

### A. 消费函数已锁定（动态+静态交叉验证）
**0x10014afe = 转场/图层的合成绘制函数**：
- 入口两次 `rep movs`（0x10014b12 / 0x10014b32）把 this+0x8c 的 **14-dword 参数块**
  拷到栈上两份 —— 参数块就是 blit 参数结构。
- 块字段语义（动态实测+静态对照）：
  - f0 = blit flags。普通窗口层 f0=0x80000050；转场层 f0=0x810。
    0x40=颜色alpha、0x100=tone、0x800=blend模式类、0x1000=源alpha逐像素、
    0x20000=flash、0x4000/0x8000=XY翻转（0x10096680 分发器解析）。
  - 文件转场环写入：f2=0x1000、f4=0x100、f5=vague、f6=threshold(每帧+0x100/dur)。
- 动态证据：转场帧内 blit(0x10096680) 调用者直方图中出现稀疏的 0x10015091
  （= 0x10014afe 内部的缩放 blit 调用点），普通帧没有。
- 每帧权重流（f13 递减 8）来自 transition 默认环 0x100082d7（frida ret=0x82dc，
  20 次 = 20 帧），目标层 = singleton+0x258（实例 0x5ba2858）确认。
- 0x10018463/0x10018518（每帧 567/566 次）是**窗口光标闪烁/动画**同步
  （÷0x28=40 帧周期），与转场无关，排除。

### B. 渲染后端架构修正
- 0x10097090：通过 **combase!CoCreateInstance(CLSID_DirectDraw7
  {3C305196-50DB-11D3-9CFE-00C04FD930C5})** 创建设备 —— 原版是
  **DirectDraw7（经 COM，无 ddraw.lib 导入）**；IID_IDirect3D7 常量也在
  （0x10156010/0x10156020）。
- 屏幕(0xD4, ctor 0x10097680, vtable 0x1014056c)：+0xc4 = DD 设备对象、
  +0xc8/+0xcc = DD 表面（前/后缓冲）。
- 软件路径 blit 0x10096680 = **旗标分发器**：按 flags 调 [this+0x24] 像素渲染器
  对象的虚方法（+0x38=逐像素alpha、+0x34/+0x30/+0x2c/+0x24/+0x1c/+0x14/+0x10/+0xc/+0x4
  等分支；0x4000/0x8000=翻转）。
- **per-pixel 遮罩混合内核 = [layer+0x24] 渲染器对象的虚方法**（特别是 +0x38
  逐像素 alpha 分支与 0x1000 旗标对应的源alpha路径）。
- 转场合成链：Graphics.transition(文件) → 遮罩文件画进 +0x258 层图像(0x10096d50)
  → 每帧 0x9a280 写参数块(f2/f4/f5/f6) → 0x10014afe 读块 → 0x10096680 分发 →
  渲染器 blend 内核逐像素混合 冻结图/实况/遮罩。

## 剩余工作（下一轮）
1. 找到 layer+0x24 渲染器对象的构造与虚表（0x100981c0 EnterCriticalSection 附近
   的对象即疑似），锁定 +0x38 / 0x1000-源alpha 的混合内核地址。
2. 逐指令解码内核中 threshold/vague 的用法（预期: 每像素 alpha =
   clamp((mask*256 − threshold*mask?)/vague…) —— 以解码结果为准，不猜）。
3. 实装 + 自制灰度遮罩对拍。

## 动态工具要点
- frida: spawn→暂停→轮询 findModuleByName→hook→resume→export_evidence_bundle。
- 本机 frida 运行时无 Memory.readPointer/readS32 —— 用 ptr.readS32()/readPointer()。
- MemoryAccessMonitor 按页触发（噪声大，不适合本场景，弃用）。
- RVA 备忘: 0x1009a280 写块 / 0x10096680 blit 分发 / 0x10014afe 合成绘制 /
  0x10014770 移位写 / 0x1016efe4 单例(+0x258 层) / 0x10104da4 层虚表。
