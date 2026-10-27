# M5-n 工作笔记 — 文件转场遮罩解码（进行中）

状态: 消费函数尚未锁定到指令级。以下是已确证的白盒事实与下步计划。

## 已确证（静态，全部指令级）
1. 原版为纯软件渲染: 导入表无 d3d9/ddraw/msimg32/AlphaBlend（kernel32/user32/gdi32/
   winmm/ws2_32/msacm32/advapi32/combase/ntdll）。
2. Graphics 单例 0x1016efe4:
   - +0x244 屏幕(0xD4, ctor 0x10097680) +0x250/+0x254/+0x258 三个同层类对象
     (0x2B8 字节, ctor 0x10015ad0, vtable 0x10104da4) +0x25c Input(0xA0, ctor 0x100138a0)
   - 层类字段: +0x8c 14-dword 参数块, +0x20 显示模式(vtable[1] 写), +0x74 脏标记,
     +0xE8/+0xF0 double=1.0(缩放), +0x104=0xb4/+0x108=0x168/+0x120=0x80
   - 层类虚表 0x10104da4: [0]dtor 0x10015a10, [1]0x10098ce0(设 mode: [ecx+0x20]=arg,
     [ecx+0x74]|=5), [2]0x10098960(set x/y), [4]0x10098cb0(set z), [5]0x100989a0,
     [6]0x1009a300(visible), [11]0x10015940, [12]0x10015820, [14]0x10015e70
3. 0x1009a280(layer, src, flag): 14-dword 块拷到 layer+0x8c; flag!=0 时 0x10098a10(layer,0)。
   0x10014770(layer, prop, v): 拷块→block[0]=旧block[13]→block[13]=v→写回+调用。
   (动态确认: 每次调用 block[0] 是上次 block[13] 的移位)
4. fadeout/fadein/transition 默认环公式已还原并实装(M5-m)。
5. transition 文件环 0x10008238: 文件→纹理→0x10096560/0x10096d50 绘入 +0x258 层,
   每帧 0x1009a280(+0x258,1,&param), param 含 0x1000/0x100/vague/阈值(每帧 +0x100/dur)。
6. 0x100112b0 = Window 构造(4 层引擎 = 背景/框/内容/光标, engine 0x94 字节, ctor 0x10019680)。
7. 低层 blit = 0x10096680(裁剪+缩放), 运行时所有绘制汇聚于此。
   实测转场期间调用者: 0x100177b7(主力,普通精灵) + 0x10015091(内部精灵缩放路径, 0x10014afe 函数)。
8. 动态实测(原版 Frida):
   - 窗口层参数块 f0=0x80000050, f13=255(不透明度), 仅变化时写。
   - 菜单开启转场期间, 若干层每帧收 f13 递减序列 223,215,207,...(-8/帧) — 权重流。
   - singleton+0x258 层(vtable 0x10104da4 确认)在转场帧未被 vtable[11]/[12]/[14] 调用,
     9a280 也不写它(写入只在 freeze/transition 进入时)。

## 未决
- 文件转场 per-pixel 遮罩混合的准确公式与位置。
- 0x100177b7 所在函数与 0x10014afe(内部精灵绘制) 是否承担遮罩合成。
- 递减权重流(223→) 的写入者与 f0=0x810 块的准确语义。

## 下步
1. Frida 钩 0x1009a280 记录**返回地址**(定位每帧权重流的调用者函数)。
2. 钩 0x10096680 记录参数(源/目标 rect+flag), 对照冻结纹理指针找出合成调用。
3. 锁定函数后按 0x10096680 内部分支(critical section + blit 模式)解码 per-pixel 公式。
4. 实装 + 用自制灰度遮罩图做文件转场对拍。

工具: frida session 流程已跑通 (spawn→pause→poll module→hook→resume→export_evidence_bundle);
hook 模板见 frida_evidence8.json 会话。注意本机 frida 运行时无 Memory.readPointer,
用 ptr.readS32()/readPointer()。RVA: 0x1009a280=param writer, 0x10096680=blit, 0x1016efe4=singleton。
