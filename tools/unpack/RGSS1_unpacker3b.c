/*
 * unpacker3.c — 调试器式脱壳: 硬件断点截获 OEP, 抓取未执行 DllMain 的纯净镜像
 * 用法 (父): unpacker3.exe <packed.dll> <outdir> <oep_rva_hex>
 *       (子): unpacker3.exe --child <packed.dll>
 */
#include <windows.h>
#include <tlhelp32.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DLLBASE 0x10000000u

static void dump_one_module_exports(HANDLE hp, DWORD base, const char *modname, FILE *f)
{
    unsigned char hdr[0x400];
    SIZE_T rd;
    if (!ReadProcessMemory(hp, (LPCVOID)base, hdr, sizeof hdr, &rd)) return;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)hdr;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
    DWORD nto = *(DWORD *)(hdr + dos->e_lfanew);
    unsigned char nth[0x100];
    if (!ReadProcessMemory(hp, (LPCVOID)(base + nto), nth, sizeof nth, &rd)) return;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)nth;
    DWORD edrva = nt->OptionalHeader.DataDirectory[0].VirtualAddress;
    DWORD edsz  = nt->OptionalHeader.DataDirectory[0].Size;
    DWORD img   = nt->OptionalHeader.SizeOfImage;
    if (!edrva || edrva >= img) return;
    unsigned char *ed = malloc(edsz ? edsz : 0x1000);
    if (!ReadProcessMemory(hp, (LPCVOID)(base + edrva), ed, edsz, &rd)) { free(ed); return; }
    IMAGE_EXPORT_DIRECTORY *exp = (IMAGE_EXPORT_DIRECTORY *)ed;
    #define R32(rva) ReadPr32(hp, base, (rva), img)
    /* 小工具: 读子进程 dword */
    DWORD tabn[512], tabo[512];
    DWORD n = exp->NumberOfNames > 512 ? 512 : exp->NumberOfNames;
    ReadProcessMemory(hp, (LPCVOID)(base + exp->AddressOfNames), tabn, 4 * n, &rd);
    ReadProcessMemory(hp, (LPCVOID)(base + exp->AddressOfNameOrdinals), tabo, 2 * n, &rd);
    DWORD nf = 0;
    DWORD *fncs = malloc(4 * (exp->NumberOfFunctions ? exp->NumberOfFunctions : 1));
    ReadProcessMemory(hp, (LPCVOID)(base + exp->AddressOfFunctions), fncs,
                      4 * exp->NumberOfFunctions, &rd);
    BYTE *covered = calloc(exp->NumberOfFunctions + 1, 1);
    char nm[256];
    for (DWORD i = 0; i < n; i++) {
        if (!tabn[i] || tabn[i] >= img) continue;
        if (!ReadProcessMemory(hp, (LPCVOID)(base + tabn[i]), nm, sizeof nm - 1, &rd)) continue;
        nm[sizeof nm - 1] = 0;
        for (int q = 0; q < (int)sizeof nm; q++) if (!nm[q]) { break; } else if (nm[q] < 0x20 || nm[q] > 0x7e) nm[q] = '?';
        DWORD frva = (tabo[i] < exp->NumberOfFunctions) ? fncs[tabo[i]] : 0;
        int fwd = (frva >= edrva && frva < edrva + edsz);
        char tgt[64] = "";
        char tgtfn[128] = "";
        if (fwd) {
            char fs[192];
            if (ReadProcessMemory(hp, (LPCVOID)(base + frva), fs, sizeof fs - 1, &rd)) {
                fs[sizeof fs - 1] = 0;
                char *dot = strchr(fs, '.');
                if (dot && dot - fs < 32) {
                    memcpy(tgt, fs, dot - fs); tgt[dot - fs] = 0;
                    for (char *q = tgt; *q; q++) *q = (char)tolower((unsigned char)*q);
                    lstrcpynA(tgtfn, dot + 1, sizeof tgtfn);
                }
            }
        }
        if (tgt[0])
            fprintf(f, "%s\t%08lX\t%s\t%d\tFWD\t%s\t%s\n", modname,
                    (unsigned long)frva, nm, (int)tabo[i] + (int)exp->Base, tgt, tgtfn);
        else
            fprintf(f, "%s\t%08lX\t%s\t%d\t%s\n", modname, (unsigned long)frva, nm,
                    (int)tabo[i] + (int)exp->Base, fwd ? "FWD" : "");
        if (tabo[i] < exp->NumberOfFunctions) covered[tabo[i]] = 1;
        nf++;
    }
    for (DWORD k = 0; k < exp->NumberOfFunctions; k++)
        if (!covered[k] && fncs[k])
            fprintf(f, "%s\t%08lX\t#%lu\t%lu\t\n", modname, (unsigned long)fncs[k],
                    (unsigned long)(k + exp->Base), (unsigned long)(k + exp->Base));
    free(covered); free(fncs); free(ed);
}

static int run_child(const char *dll)
{
    HMODULE h = LoadLibraryA(dll);
    printf("child: LoadLibrary=%p GLE=%lu\n", (void *)h, GetLastError());
    return h ? 0 : 1;
}

static int run_parent(const char *dll, const char *outdir, DWORD oepRva)
{
    char cmd[1024], mypath[MAX_PATH];
    GetModuleFileNameA(NULL, mypath, MAX_PATH);
    sprintf(cmd, "\"%s\" --child \"%s\"", mypath, dll);
    STARTUPINFOA si = { sizeof si };
    PROCESS_INFORMATION pi;
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, DEBUG_PROCESS, NULL, NULL, &si, &pi)) {
        fprintf(stderr, "CreateProcess failed %lu\n", GetLastError());
        return 2;
    }
    int done = 0, dllLoaded = 0;
    DWORD childPid = 0;
    DEBUG_EVENT ev;
    int evcount = 0;
    while (!done && WaitForDebugEvent(&ev, INFINITE)) {
        fprintf(stderr, "ev#%d code=%u th=%u\n", ++evcount,
                (unsigned)ev.dwDebugEventCode, (unsigned)ev.dwThreadId);
        fflush(stderr);
        DWORD cont = DBG_CONTINUE;
        switch (ev.dwDebugEventCode) {
        case LOAD_DLL_DEBUG_EVENT:
            if ((DWORD)(DWORD_PTR)ev.u.LoadDll.lpBaseOfDll == DLLBASE) {
                dllLoaded = 1;
                CONTEXT ctx; ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
                HANDLE ht = OpenThread(THREAD_ALL_ACCESS, FALSE, ev.dwThreadId);
                GetThreadContext(ht, &ctx);
                ctx.Dr0 = DLLBASE + oepRva;
                ctx.Dr7 = 0x155;   /* L0+G0, 执行断点 (RW=00 LEN=00) + GD off: 用 0x1 足够, 写 0x155 保守 */
                ctx.Dr7 = 0x00000001;
                SetThreadContext(ht, &ctx);
                CloseHandle(ht);
                printf("hwbp set at OEP %08lX\n", (unsigned long)(DLLBASE + oepRva));
            }
            break;
        case EXCEPTION_DEBUG_EVENT:
            fprintf(stderr, "  exc code=%08lx addr=%08lx first=%u\n",
                (unsigned long)ev.u.Exception.ExceptionRecord.ExceptionCode,
                (unsigned long)(DWORD_PTR)ev.u.Exception.ExceptionRecord.ExceptionAddress,
                (unsigned)ev.u.Exception.dwFirstChance);
            fflush(stderr);
            if (ev.u.Exception.ExceptionRecord.ExceptionCode == EXCEPTION_SINGLE_STEP && dllLoaded) {
                fprintf(stderr, "  single-step, opening thread %u\n", (unsigned)ev.dwThreadId);
                fflush(stderr);
                HANDLE ht = OpenThread(THREAD_ALL_ACCESS, FALSE, ev.dwThreadId);
                if (!ht) { fprintf(stderr, "  OpenThread failed\n"); fflush(stderr); break; }
                CONTEXT ctx; ctx.ContextFlags = CONTEXT_FULL;
                if (!GetThreadContext(ht, &ctx)) {
                    fprintf(stderr, "  GetThreadContext failed %lu\n", GetLastError());
                    fflush(stderr); CloseHandle(ht); break;
                }
                fprintf(stderr, "  eip=%08lx (target %08lx) EAX=%08lx EBX=%08lx ECX=%08lx EDX=%08lx\n",
                        (unsigned long)ctx.Eip, (unsigned long)(DLLBASE + oepRva),
                        (unsigned long)ctx.Eax, (unsigned long)ctx.Ebx,
                        (unsigned long)ctx.Ecx, (unsigned long)ctx.Edx);
                fflush(stderr);
                if ((DWORD)ctx.Eip == DLLBASE + oepRva) {
                    fprintf(stderr, "  OEP HIT\n"); fflush(stderr);
                    done = 1;
                }
                CloseHandle(ht);
                if (!done) cont = DBG_EXCEPTION_NOT_HANDLED;
            } else {
                cont = DBG_EXCEPTION_NOT_HANDLED;
            }
            break;
        case EXIT_PROCESS_DEBUG_EVENT:
            done = 2;
            break;
        }
        if (done == 1) break;          /* OEP 命中: 不 continue, 子进程保持冻结 */
        ContinueDebugEvent(ev.dwProcessId, ev.dwThreadId, cont);
    }
    if (done != 1) {
        fprintf(stderr, "child exited before OEP hit\n");
        TerminateProcess(pi.hProcess, 1);
        return 1;
    }

    /* ---- 抓取交给独立 python 进程 (dumpat.py) ---- */
    {
        char dcmd[2048];
        sprintf(dcmd,
                "C:\\Windows\\py.exe -3 "
                "\"E:\\rgss2-rebuild\\tools\\dumpat.py\" %u \"%s\"",
                pi.dwProcessId, outdir);
        fprintf(stderr, "launch: %s\n", dcmd); fflush(stderr);
        int rc = system(dcmd);
        fprintf(stderr, "dumpat rc=%d\n", rc); fflush(stderr);
        TerminateProcess(pi.hProcess, 0);
        return rc == 0 ? 0 : 1;
    }
}

int main(int argc, char **argv)
{
    if (argc >= 3 && !strcmp(argv[1], "--child")) return run_child(argv[2]);
    if (argc >= 4) return run_parent(argv[1], argv[2], strtoul(argv[3], NULL, 16));
    fprintf(stderr, "usage: unpacker3.exe <packed.dll> <outdir> <oep_rva_hex> | --child <dll>\n");
    return 2;
}
