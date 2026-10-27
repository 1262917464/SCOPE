#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <stdio.h>
#include <tlhelp32.h>

/* 用 SEH 包住的导出反查, 任一步出界立即放弃 */
static int resolve(DWORD addr, char *out, size_t outsz)
{
    out[0] = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE) return 0;
    MODULEENTRY32 me; me.dwSize = sizeof me;
    int found = 0;
    if (Module32First(snap, &me)) {
        do {
            DWORD base = (DWORD)(DWORD_PTR)me.modBaseAddr;
            DWORD size = me.modBaseSize;
            if (addr >= base && addr < base + size) {
                BYTE *bp = (BYTE *)me.hModule;
                if (1) {
                    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)bp;
                    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(bp + dos->e_lfanew);
                    DWORD expRVA = nt->OptionalHeader.DataDirectory[0].VirtualAddress;
                    if (expRVA && expRVA < size) {
                        IMAGE_EXPORT_DIRECTORY *exp = (IMAGE_EXPORT_DIRECTORY *)(bp + expRVA);
                        DWORD nn = exp->NumberOfNames, nf = exp->NumberOfFunctions;
                        if (nn < 100000 && nf < 100000 && exp->AddressOfFunctions &&
                            exp->AddressOfNames && exp->AddressOfNameOrdinals) {
                            DWORD *funcs = (DWORD *)(bp + exp->AddressOfFunctions);
                            DWORD *names = (DWORD *)(bp + exp->AddressOfNames);
                            WORD  *ords  = (WORD *)(bp + exp->AddressOfNameOrdinals);
                            for (DWORD k = 0; k < nn; k++) {
                                if (ords[k] < nf && base + funcs[ords[k]] == addr) {
                                    _snprintf(out, outsz, "%s!%s", me.szModule,
                                              (char *)(bp + names[k]));
                                    out[outsz-1] = 0; found = 1; break;
                                }
                            }
                        }
                    }
                }
                if (!found) {
                    _snprintf(out, outsz, "%s!+0x%lX", me.szModule,
                              (unsigned long)(addr - base));
                    out[outsz-1] = 0; found = 1;
                }
                break;
            }
        } while (Module32Next(snap, &me));
    }
    CloseHandle(snap);
    return found;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    HMODULE h = LoadLibraryA(argv[1]);
    if (!h) return 1;
    DWORD base = (DWORD)(DWORD_PTR)h;
    printf("base=0x%08lX\n", (unsigned long)base);
    for (DWORD rva = 0xFF000; rva < 0xFF400; rva += 4) {
        DWORD val = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), (LPCVOID)(base+rva), &val, 4, NULL))
            continue;
        if (!val) continue;
        char nm[512] = "?";
        resolve(val, nm, sizeof nm);
        printf("  0x%08lX = %-30s\n", (unsigned long)(base+rva), nm);
    }
    return 0;
}
