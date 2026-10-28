# M5-n 文件转场遮罩解码与实装报告

> 承接 M5-m（freeze/transition/fadein/fadeout 实装，文件路径当时退化为 crossfade）。
> 本轮把原版文件转场 (Graphics.transition(dur, file, vague)) 的逐像素算法
> 解码到指令级并实装。全部结论来自离线 objdump（full_disasm.txt，基址 0x10000000）
> + Frida 动态交叉验证，无目测反推。日期 2026-10-29。

## 一、原版渲染架构（本轮定案）

- **DirectDraw7 经 COM**：0x10097090 以 CoCreateInstance(CLSID_DirectDraw7
  {3C305196-50DB-11D3-9CFE-00C04FD930C5}, combase 导入) 创建设备；
  IID_IDirect3D7 常量并存（0x10156010/0x10156020）。导入表无 d3d9/ddraw/msimg32。
- Graphics 单例 0x1016efe4：+0x244 屏幕(0xD4, ctor 0x10097680, vtable 0x1014056c,
  +0xc4=DD 设备, +0xc8/+0xcc=前后缓冲)；+0x250/+0x254/+0x258 = 三个同层类对象
  (0x2B8 字节, ctor 0x10015ad0, vtable 0x10104da4, +0x8c=14-dword 参数块)；
  +0x25c = Input(0xA0, ctor 0x100138a0, Input.update=0x10013c40)。
- blit 分发器 0x10096680：this=图像对象（+0x8→DIB 信息 (+0xe=bpp), +0x24=方法表），
  按 flags 分发到渲染器方法表（双变体 0x1015e70c→vtable 0x1014051c /
  0x1015e710→vtable 0x101404b4）。
- **排除项**：0x100183f0=窗口光标闪烁动画(÷40)；0x10019680 四层引擎=Window 构造
  （背景/框/内容/光标）；0x100112b0=Window 构造。

## 二、文件转场完整调用链（全部指令级）

```
Graphics.transition(dur, file, vague=0x28)      0x10007fb0 (argc=-1)
  文件环 0x10008238:
    遮罩文件→图像 (0x10009440/0x10096d50, 8bpp 灰度) 上传至 +0x258 层, 一次
    每帧 i: threshold = 0x100 + (i+1)*0x100/dur           (0x1000823a)
            0x1009a280(+0x258层, 1, &param)               写 14-dword 参数块
    末帧 threshold=0x200 → kernel 因 %0x200==0 跳过绘制（最后一帧直接全实况）
  层绘制链: 0x10014afe(两次 rep movs 拷参数块) → 0x10096680(分发, flags=f0=0x1000)
    → 方法表 0x101404b4 entry3 = 文件转场内核 0x100a5710
       ├ 校验: [param+8]≤0x200、[param+0xC]=vague≤0x1ff、bpp==32、%0x200≠0
       ├ 0x100ae760: param[0]&0x10 → idx=[param+0x34] (f13), >0xFF 该层不画;
       │             否则 idx=0xff   （文件路径 f0=0x1000 → idx 恒 0xff）
       ├ 因子表构造 (256B):
       │   shifted = [param+8] − 0x100      （threshold>0x100 分支, phase=0xff）
       │   lut[i] = i<shifted        → LUT2[255][0]      (=0)
       │            i<shifted+vague  → LUT2[255][(i−shifted)*255/vague] （渐变带）
       │            其余             → LUT2[255][0xff]   (=255)
       │   LUT2 (0x1019ca90, 运行时构建 0x100a98a1): LUT2[a][v] = (a*v+127)/255
       │   → 本路径 lut[i] = 0 / (i−shifted)*255/vague / 255
       └ 终绘: 0x1009fe00 —— 与 draw_text 同一个 MMX 混合器 (F 表 0x1018bb90,
         F[a]=a+1): 逐像素 a = lut[mask_byte],
         out = dst + (src − dst) × F[a] >> 8 （四通道, packuswb 钳制）
         dst=实况帧([ebp+8] 图像), src=冻结图([ebp+0x10] 图像)
```

**语义**：遮罩字节低者先切到实况；vague 是渐变带宽度；threshold 每帧推进把带
从 mask=0 扫到 255。方向由 phase（threshold>0x100 → 0xff）固定。

## 三、重建实装（graphics_classes.c）

- `gfx_transition` 文件分支：rgss_load_image 读遮罩 → 逐像素亮度（原版 8bpp；
  32bpp 源取 (r*77+g*151+b*28)>>8，灰度遮罩下与原版无差）→ 每帧
  shifted=(i+1)*0x100/dur → `gfx_step_frame`。
- `gfx_apply_display_rule` 新增文件转场分支：按上表构造因子 →
  `out = dst + (frozen − dst)*(a+1)>>8`（0x1009fe00 公式, 钳制）。
- 顺带：gamemain.c 增加 RGSS_TEST_EVAL 调试钩（每脚本段前幂等 eval，
  RGSS_DEBUG_TOOLS 门控）——游戏入口段会阻塞主循环，故须逐段前注入。

## 四、验证（ftprobe + TestMask 灰度梯度 0→255, dur=30, vague=60）

驱动：Graphics.update 包裹层（帧 10 freeze、帧 25 画红屏、帧 395 前另测）→
freeze 标题为 src、纯红 Sprite 为 dst、文件转场 30 帧。逐帧截屏 110 张。

- 红色（实况）自低 mask 侧出现，5 个截屏周期（≈500ms=30 帧@60fps）完成 ✓
- 渐变带位置实测 114/223/353/464/543px，与解码公式 shifted=(k+1)*256/30
  映射像素 ×543/255（每截屏周期 6 内部帧 ≈ 109px 步进）完全吻合 ✓
- 方向：低 mask（左）先变实况，与解码 phase=0xff 一致 ✓
- 回归 difftest：f00 1.19%（持平）、f01 74.88%/f05 73.85%（既有地图自动图块
  基线，无新增差异）✓

## 五、已知差异

1. 遮罩 32bpp→亮度公式未逐指令解码（原版为 8bpp 调色板转换）；自制灰度遮罩
   下无差异。非灰度彩色遮罩的转换曲线可能不同。
2. 0x1009ffb0（flags&0x800 镜像/反相变体）未实装（文件路径 f0=0x1000 不含 0x800）。

## 六、工具沉淀

- `tools/probe/ftprobe.c`：RGSS_TEST_EVAL + 逐帧整屏 BMP。
- frida 流程模板与 RVA 备忘见工作笔记；本机 frida 无 Memory.readPointer
  （用 ptr.readS32()）；每调用 send 会压垮进程（批量直方图替代）。
- 中间证据：local/build/frida_evidence*.json、local/test/ft_shots/。
