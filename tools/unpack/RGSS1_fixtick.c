/* RGSS1_fixtick.c — 真 GetTickCount 转发（供 asm 桩调用，cdecl，EAX 返回） */
#include <windows.h>

unsigned long get_tick(void)
{
    return GetTickCount();
}
