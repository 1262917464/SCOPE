/*
 * debug.h — 调试辅助总开关（B44）
 *
 * ⚠ 这一切都不符合原版：原版 DLL 无任何日志/崩溃转储副作用。
 * 本开关只为重建开发期服务；声称 1:1 的构建必须用 -DRGSS_DEBUG_TOOLS=0。
 *
 *   RGSS_DEBUG_TOOLS=1 (默认): 日志可用, 启动时清理旧日志, 崩溃转储开启
 *   RGSS_DEBUG_TOOLS=0:        所有日志写入编译为空操作 (fopen 恒 NULL,
 *                              既有 `if (f)` 守卫自动跳过), 无崩溃转储
 *
 * 另: 环境变量 RGSS_DEBUG_CRASH_TEST=1 时启动即空指针自毁,
 *     用于验证崩溃转储通路 (仅 RGSS_DEBUG_TOOLS=1 时编译)。
 */
#ifndef RGSS_DEBUG_H
#define RGSS_DEBUG_H

#include <stdio.h>

#ifndef RGSS_DEBUG_TOOLS
#define RGSS_DEBUG_TOOLS 1
#endif

#if RGSS_DEBUG_TOOLS
#define RGSS_LOG_FOPEN(name) fopen((name), "a")
void rgss_debug_cleanup(void);   /* 启动时删除历史日志 (gamemain.c) */
#else
#include <windows.h>
#define RGSS_LOG_FOPEN(name) ((FILE *)NULL)
static inline void rgss_debug_cleanup(void) { (void)0; }
#endif

/* 已知日志文件清单 (rgss_debug_cleanup 与文档共用) */
static const char *const rgss_debug_log_names[] = {
    "rgss_text.log", "rgss_phase.log", "rgss_render.log", "rgss_caller.log",
    "rgss_input.log", "rgss_window.log", "rgss_tilemap.log", "rgss_init.log",
    "rgss_rebuild.log", "rgss_trace.log", "rgss_crash.log", "rgss_error.log", "dbg.txt", NULL
};

#endif /* RGSS_DEBUG_H */
