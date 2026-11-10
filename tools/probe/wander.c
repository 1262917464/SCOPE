/* wander.c — 对已聚焦的 Game.exe 随机按方向键漫游 N 秒（配合 frida 采样）
 * 用法: wander.exe [秒数=90] */
#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(int argc, char **argv)
{
    int secs = (argc > 1) ? atoi(argv[1]) : 90;
    WORD keys[4] = { VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT };
    int i, iters = secs * 3;
    srand((unsigned)time(NULL));
    for (i = 0; i < iters; i++) {
        WORD vk = keys[rand() % 4];
        keybd_event((BYTE)vk, (BYTE)MapVirtualKeyA(vk, 0), 0, 0);
        Sleep(120);
        keybd_event((BYTE)vk, (BYTE)MapVirtualKeyA(vk, 0), KEYEVENTF_KEYUP, 0);
        Sleep(210);
    }
    printf("wander done\n");
    return 0;
}
