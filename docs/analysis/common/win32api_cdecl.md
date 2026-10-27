# Win32API 调用约定陷阱（mingw 重编必读）

> 提取自重构前根 `PROGRESS.md`（2026-10-27）。对应实现：`src/runtimes/rgss2/ext/Win32API.c`。

## 问题

Ruby 1.8.1 上游 `ext/Win32API` 用结构体塞参 + 通用函数指针调用，依赖编译器按特定约定生成弹栈代码：

- 原版 RGSS DLL 是 **MSVC** 编译的，该写法能歪打正着；
- 用 **mingw gcc** 重编时，gcc 按 **cdecl**（调用方清栈）生成调用序列，而被调用的 Windows API 是 **stdcall**（被调方清栈）——两边都清栈，栈指针多弹一次 → **栈失衡段错误**。

## 重建版修法

`Win32API.c` 的 `Win32API_Call` 按参数个数 `switch (nimport)` 显式分派到 16 个 `long (WINAPI *)()`（stdcall）原型（case 0..15 + default 16），让 gcc 生成与被调方约定一致的调用序列。

## 归属提示（ADR-0001）

此修补绑定的是 **Ruby 版本 + 编译器**，不是 RGSS 版本。RGSS1 同样用 Ruby 1.8.1，将来建源时这份文件是 `src/shared/` 的**首个候选**——届时先跑对拍确认行为一致再上提，不要复制两份各自演化。
