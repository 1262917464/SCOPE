# M5 中期报告 — 主循环全解 + Init 归属（进行中）
> 注：本文写于 2026-10-27 仓库重构前；文中 `backup/` `unpacked/` `build/` `test/` `third_party/` 等路径现位于 `local/` 下，`src/rgss_*.c` 现为 `src/runtimes/rgss2/*.c`，`tools/` 已按流水线分桶（unpack/pe/analysis/probe/verify）。


日期：2026-10-26 ｜ 状态：主循环语义完整，Init 1/5 归属，逐类还原待续

## 1. 主循环 sub_1000AFD0(scripts_fname) — 完整语义（已反编译 0x43B 全函数）

```
if 前置检查 sub_1000ACC0(fname) 成功:
  eval("Graphics._reset")
  while (true):                                  # ★重启大循环
      while (GetAsyncKeyState(VK_F12) & 0x8000): # F12 按住时: 泵消息等松开
          frame->处理消息(0x10001640)
          frame->窗口存活?(0x10001890)==0 → 退出
      eval("scripts_fname = ''")                 # 变量初始化
      scripts_fname = 读 Game.ini [Scripts]      # sub_10008C70
      eval("$RGSS_SCRIPTS = load_data(scripts_fname);
            $RGSS_SCRIPTS.each { |s| s[3,0] = Zlib::Inflate.inflate(s[2]) };
            $RGSS_SCRIPTS.size")
      n = 结果                                    # 脚本段数
      for i in 0...n:
          eval("'Section%03d' % i")               # 包装文件名(错误栈显示用)
          src = eval("$RGSS_SCRIPTS[%d][3]" % i)  # 解压后源码
          g_running(0x10173478) = 1
          eval(src, binding="Section%03d")        # ★逐段执行游戏脚本
          g_running = 0
      异常 → "$!.type.to_s" == "Reset" ?
          是 → continue 大循环(F12 重启)          # RGSS Reset 异常机制实锤
          否 → 错误消息(GetWindowText风格) + 弹窗(0x100015A0=MessageBoxW 包装) → return
  # 正常跑完(脚本调用 exit 等):
  终点清理×6: sub_100048F0/10010EA0/1000E080/1000F970/1000BF70/10012100
  eval("Graphics._reset;Input.update;Audio.me_stop;Audio.se_stop;
        Audio.bgm_stop;Audio.bgs_stop;GC.start;")
```

**关键事实**：
- F12 重启 = `Reset` 异常 + 大循环；VK_F12 直接码 123
- `$RGSS_SCRIPTS` 条目结构: `[?, name, zlib_deflated, inflated_src]`
- `0x10173478` = 脚本运行标志（RGSSFinalize 清零的正是它）
- `0x1015B0C0` = 脚本装载中标志（错误消息路径分支用）
- 栈变换 SEH cookie（dword_1016D690 异或栈 Canary，MSVC /GS）

## 2. Init 模块归属（1/5 完成）

| Init 函数 | 归属 | 证据 |
|-----------|------|------|
| sub_10003B60 | **Init_Audio** | `rb_define_module("Audio")` + 11 方法：bgm_play@0x100035A0, bgm_stop@0x3460, bgm_fade@0x3480, bgs_play@0x3710, bgs_stop@0x34C0, bgs_fade@0x34E0, me_play@0x3880, me_stop@0x3520, me_fade@0x3540, se_play@0x39F0, se_stop@0x3580 |
| sub_1000EFA0 | 待定（候选 Bitmap/Color/Rect/Font 组） | |
| sub_10006950 | 待定（候选 Graphics/Input 组） | |
| sub_10010210 | 待定（候选 Sprite/Plane/Viewport 组） | |
| sub_1000C910 | 待定（候选 Tilemap/Window/Table 组） | |

（注：Init 调用顺序在 sub_10009E60 中为 3B60→EFA0→6950→10210→C910，与 Ruby API
名字符串区的 Audio→Bitmap→Font→Graphics→Input→…→Window 排列对照即可定归属。）

## 3. 剩余工作（M5 长尾）

1. Init#2–5 归属（各 1 次反编译）
2. CRx*/Nx/CSx 逐类虚函数还原（从 RxSprite 13 虚函数开始，方法名表已就绪）
3. Nx 渲染管线 + JPEG 库定名；CSx 音频三路读取器
4. src/ 集成：最终 DLL 骨架（Ruby 静态库 + Win32API + RGSS 绑定层逐步生长）
5. RGSSGC/RGSSAudioInitialize/RGSSSetupFonts 语义对齐
