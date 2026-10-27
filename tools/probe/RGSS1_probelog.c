/* probelog.c — 探针日志函数(与 probe.S 配套) */
#include <windows.h>

void log_call(const char *tag, void *retaddr, unsigned long *args)
{
    HANDLE f = CreateFileA("RGSS1_probe.log", FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    char buf[512];
    int len = wsprintfA(buf,
        "%s ret=%08lx a0=%08lx a1=%08lx a2=%08lx a3=%08lx a4=%08lx a5=%08lx\r\n",
        tag, (unsigned long)(DWORD_PTR)retaddr,
        args[0], args[1], args[2], args[3], args[4], args[5]);
    DWORD wr;
    WriteFile(f, buf, len, &wr, NULL);
    CloseHandle(f);
}
