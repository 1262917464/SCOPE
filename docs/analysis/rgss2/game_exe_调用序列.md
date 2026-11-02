# Game.exe 调用序列逆向（0x4012f0 WinMain 主流程）

> 2026-10-29，blt 对拍探针（tools/verify/bltcheck.c）依赖此序列搭建进程内环境。
> Game.exe 135KB，动态 LoadLibraryW 加载 RGSS DLL（导入表只有 CRT 依赖）。

## 关键顺序（与探针相关的部分）

1. **窗口在 RGSSInitialize2 之前创建**：
   - `RegisterClassW("RGSS Player")`，wc.style=8 (CS_HREDRAW|CS_VREDRAW)
   - `CreateWindowExW(WS_EX_WINDOWEDGE(0x100), "RGSS Player", Title, 0xCA0000,
     CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, …)`（0x401652）
   - `AdjustWindowRectEx({0,0,544,416})` → `SetWindowPos`(0x14=NOZORDER|NOACTIVATE)
   - `ShowWindow(SW_SHOW)`
2. `GetPrivateProfileStringW(Game, Title/Library/RTP/Scripts)`（0x40153f-0x4015bb）
3. DLL 名解析（0x401110）：三连尝试 `LoadLibraryW`：
   `.<DLL名>` → `<DLL名>` → `C:\Windows\System32\.<DLL名>`（均失败→"Cannot run on this version of Windows"）
4. `GetProcAddress` 六件套（0x401250）：RGSSInitialize2 / RGSSFinalize /
   RGSSGameMain / RGSSEval / RGSSSetupRTP / RGSSSetupFonts
5. `RGSSSetupRTP(rtp值宽串, buf, 0x400)`（0x4018ae，**实为 3 参**，此前"无参数"结论作废）
6. `RGSSFinalize()`（0x4018f9）→ **`RGSSInitialize2(hwnd)`（0x401900，收到的就是第 1 步的窗口）**
7. `RGSSEval` 数次（0x401946-0x401998）：
   `$TEST = true/false`、`$BTEST = true/false`（按 Game.exe 参数 test/btest/none 分支）
8. `RGSSSetupFonts(L"", RTP路径, …)`（0x4019b0，3 参）
9. **`RGSSGameMain(hwnd, 0, 0)`**（同一线程，非新线程）

## RGSSEval 的线程契约（重要）

Game.exe 在 `RGSSGameMain` **内部**的 `WAIT_TIMEOUT` 消息循环里调用 `RGSSEval`
（eval 请求入队 → GameMain 线程泵消息时消费）。**跨线程直接调 RGSSEval 与脚本
并发会崩**（b19test 时代已踩过：先关窗收工再 eval 是 workaround）。
探针正确姿势：eval 排队，由 GameMain 线程（或其收工后的消费循环）执行。

## 探针教训

- 进程内对**原版** DLL 跑 RGSSGameMain（假窗口/b19test 模式）会在 Graphics
  后备缓冲初始化处崩（0x1003703B，渲染目标全局 0x10187ed0 为 NULL）——
  原版黑盒对拍一律走 **Game.exe 端到端**（注入自测 Scripts.rvdata），
  rvdata 用重建 DLL 进程内 Marshal.dump 生成（tools/verify/gen_blt_rvdata.c）。
- 重建版 DLL 进程内 b19test 模式正常（其 GameMain 不依赖该初始化顺序）。
