/* reset.c — 对前台 RGSS 窗口发 F12 (RGSS Reset) + 2.5s 后 Enter (新游戏) */
#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <stdio.h>
int main(void)
{
    keybd_event(VK_F12, (BYTE)MapVirtualKeyA(VK_F12, 0), 0, 0);
    Sleep(80);
    keybd_event(VK_F12, (BYTE)MapVirtualKeyA(VK_F12, 0), KEYEVENTF_KEYUP, 0);
    Sleep(2500);
    keybd_event(VK_RETURN, (BYTE)MapVirtualKeyA(VK_RETURN, 0), 0, 0);
    Sleep(90);
    keybd_event(VK_RETURN, (BYTE)MapVirtualKeyA(VK_RETURN, 0), KEYEVENTF_KEYUP, 0);
    printf("reset sent\n");
    return 0;
}
