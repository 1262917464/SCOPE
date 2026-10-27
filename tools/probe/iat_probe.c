/* iat_probe.c — 进程内读原版 DLL 的 IAT 并反查每个槽位的真实函数名
 * 用法: iat_probe.exe <原版RGSS202E.dll路径>
 * 原理: LoadLibrary 让 ASPack 自解压 → 读自身映像 0x100FF000..0x100FF400
 *       的 IAT 槽位 → 对每个槽位用已加载模块的导出表反查名字
 */
#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <tlhelp32.h>
#include <psapi.h>

typedef int (*FnInit)(int);

/* 把一个地址解析成 "模块名!导出名" */
static int resolve_addr(DWORD addr, char *out, size_t outsz)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE) return 0;
    MODULEENTRY32 me; me.dwSize = sizeof me;
    int found = 0;
    if (Module32First(snap, &me)) {
        do {
            DWORD base = (DWORD)(DWORD_PTR)me.modBaseAddr;
            DWORD size = me.modBaseSize;
            if (addr >= base && addr < base + size) {
                /* 在该模块导出表里反查 */
                HMODULE hm = me.hModule;
                ULONG_PTR *funcs = NULL;
                DWORD *names = NULL, *ords = NULL, nname = 0, nfunc = 0;
                BYTE *basep = (BYTE *)hm;
                IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)basep;
                IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(basep + dos->e_lfanew);
                DWORD expRVA = nt->OptionalHeader.DataDirectory[0].VirtualAddress;
                int matched = 0;
                if (expRVA) {
                    IMAGE_EXPORT_DIRECTORY *exp =
                        (IMAGE_EXPORT_DIRECTORY *)(basep + expRVA);
                    nname = exp->NumberOfNames;
                    nfunc = exp->NumberOfFunctions;
                    funcs = (ULONG_PTR *)(basep + exp->AddressOfFunctions);
                    names = (DWORD *)(basep + exp->AddressOfNames);
                    ords  = (DWORD *)(basep + exp->AddressOfNameOrdinals);
                    for (DWORD k = 0; k < nname; k++) {
                        DWORD fn = funcs[ords[k]];
                        if (base + fn == addr) {
                            _snprintf(out, outsz, "%s!%s", me.szModule,
                                      (char *)(basep + names[k]));
                            out[outsz-1] = 0;
                            matched = 1;
                            break;
                        }
                    }
                }
                if (!matched)
                    _snprintf(out, outsz, "%s!+0x%lX (nfunc=%lu nname=%lu)",
                              me.szModule, (unsigned long)(addr - base),
                              (unsigned long)nfunc, (unsigned long)nname);
                out[outsz-1] = 0;
                found = 1;
                break;
            }
        } while (Module32Next(snap, &me));
    }
    CloseHandle(snap);
    return found;
}

int main(int argc, char **argv)
{
    if (argc < 2) { printf("usage: %s <dll>\n", argv[0]); return 2; }
    SetConsoleOutputCP(65001);
    HMODULE h = LoadLibraryA(argv[1]);
    if (!h) { printf("load fail %lu\n", GetLastError()); return 1; }
    /* 不调 RGSSInitialize2: IAT 在加载器填好, ASPack 在 DllMain 里自解压,
     * 之后即可读; init(0) 在无窗口环境下会崩 */

    DWORD base = (DWORD)(DWORD_PTR)h;
    printf("dll base=0x%08lX\n", (unsigned long)base);
    /* 重点区域: gdi32 块 (0xFF044 附近) 与 0xFF060-0x100FF070 */
    DWORD offs[] = {
        0xFF044, 0xFF048, 0xFF04C, 0xFF050, 0xFF054, 0xFF058, 0xFF05C,
        0xFF060, 0xFF064, 0xFF068, 0xFF06C, 0xFF070,
        0xFF064, 0xFF068, 0xFF06C
    };
    const char *tag[] = {
        "猜测: AddFontResourceExW", "猜测: BitBlt", "猜测: CreateCompatibleDC",
        "猜测: CreateDIBSection", "猜测: CreateFontIndirectW", "猜测: DeleteDC",
        "猜测: DeleteObject", "猜测: EnumFontFamiliesExW", "猜测: GetDeviceCaps",
        "猜测: GetGlyphOutlineW", "猜测: GetTextMetricsW", "猜测: SelectObject",
        "", "", ""
    };
    int n = sizeof offs / sizeof offs[0];
    for (int i = 0; i < n; i++) {
        DWORD rva = offs[i];
        DWORD val = 0;
        ReadProcessMemory(GetCurrentProcess(), (LPCVOID)(base + rva), &val, 4, NULL);
        char name[512] = "?";
        if (val) resolve_addr(val, name, sizeof name);
        printf("  IAT 0x%08lX = 0x%08lX  %-28s %s\n",
               (unsigned long)(base + rva), (unsigned long)val,
               i < 12 ? tag[i] : "", name);
    }
    /* 顺带把整个 0xFF000-0xFF400 非零槽位都列出来 */
    printf("\n--- 全量 IAT (0xFF000..0xFF400, 非零) ---\n");
    for (DWORD rva = 0xFF000; rva < 0xFF400; rva += 4) {
        DWORD val = 0;
        ReadProcessMemory(GetCurrentProcess(), (LPCVOID)(base + rva), &val, 4, NULL);
        if (!val) continue;
        char name[512] = "?";
        resolve_addr(val, name, sizeof name);
        printf("  0x%08lX = %-34s\n", (unsigned long)(base + rva), name);
    }
    return 0;
}
