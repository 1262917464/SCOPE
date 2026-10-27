/* smoke.c — 脱壳 DLL 加载冒烟测试 / unpacked DLL load smoke test */
#include <windows.h>
#include <stdio.h>

static LONG WINAPI veh(EXCEPTION_POINTERS *ep)
{
    CONTEXT *c = ep->ContextRecord;
    static int once = 0;
    if (once) return EXCEPTION_CONTINUE_SEARCH;
    once = 1;
    fprintf(stderr, "EXC %08lx at EIP=%08lx\n",
            ep->ExceptionRecord->ExceptionCode,
            (unsigned long)(DWORD_PTR)c->Eip);
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION)
        fprintf(stderr, "  access %08lx type=%lu\n",
                (unsigned long)ep->ExceptionRecord->ExceptionInformation[1],
                (unsigned long)ep->ExceptionRecord->ExceptionInformation[0]);
    fprintf(stderr, "  eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx esi=%08lx edi=%08lx\n"
                    "  ebp=%08lx esp=%08lx\n",
            (unsigned long)c->Eax, (unsigned long)c->Ebx, (unsigned long)c->Ecx,
            (unsigned long)c->Edx, (unsigned long)c->Esi, (unsigned long)c->Edi,
            (unsigned long)c->Ebp, (unsigned long)c->Esp);
    /* 扫栈: 落在 0x10000000-0x10206000 (我们的DLL) 的值 = 可能的返回地址 */
    unsigned long *sp = (unsigned long *)(DWORD_PTR)c->Esp;
    for (int i = 0; i < 96; i++) {
        unsigned long v = sp[i];
        if (v >= 0x10000000 && v < 0x10206000)
            fprintf(stderr, "  stack[%d]=%08lx (rva %06lx)\n", i, v, v - 0x10000000);
    }
    fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH;
}

int main(int argc, char **argv)
{
    AddVectoredExceptionHandler(1, veh);
    HMODULE h = LoadLibraryA(argv[1]);
    if (!h) {
        fprintf(stderr, "LoadLibrary FAILED, GLE=%lu\n", GetLastError());
        return 1;
    }
    printf("LoadLibrary OK at %p\n", (void *)h);
    const char *names[] = {"RGSSGameMain", "RGSSInitialize2", "RGSSEval",
                           "regex_new", "RGSSGetStringUTF8"};
    for (int i = 0; i < 5; i++) {
        FARPROC p = GetProcAddress(h, names[i]);
        printf("  %-20s -> %p %s\n", names[i], (void *)p, p ? "" : "(MISSING)");
    }
    FreeLibrary(h);
    printf("FreeLibrary OK\n");
    return 0;
}
