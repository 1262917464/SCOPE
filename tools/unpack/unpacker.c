/*
 * unpacker.c — ASPack DLL 动态脱壳器 / Dynamic ASPack DLL unpacker
 *
 * 原理 / Principle:
 *   LoadLibrary 让 ASPack stub 在 DllMain 期间完成解压+导入表重建,
 *   返回后直接 dump 内存镜像, 并记录所有模块导出地址映射供离线 IAT 重建。
 *
 * 用法: unpacker.exe <packed.dll> <outdir>
 * 输出: dump.bin  header.txt  exports.txt  modules.txt  modmaps.txt
 */
#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <tlhelp32.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { DWORD base; DWORD size; char name[MAX_PATH]; } ModInfo;

static void fmt_err(const char *what)
{
    char buf[256];
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, GetLastError(), 0, buf, sizeof buf, NULL);
    fprintf(stderr, "[%s] %s", what, buf);
}

/* 枚举进程内模块 / enumerate modules */
static int collect_modules(HMODULE *out, int max)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    MODULEENTRY32 me; me.dwSize = sizeof me;
    int n = 0;
    if (Module32First(snap, &me)) {
        do {
            if (n < max) {
                out[n] = me.hModule;
                n++;
            }
        } while (Module32Next(snap, &me));
    }
    CloseHandle(snap);
    return n;
}

/* 走一个模块的导出表, 把 (RVA地址, 名字, 序号) 打到文件 / walk one module's export table */
static void dump_one_module_exports(HMODULE h, const char *modname, FILE *f)
{
    BYTE *base = (BYTE *)h;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return;
    DWORD rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
    if (!rva) return;
    IMAGE_EXPORT_DIRECTORY *exp = (IMAGE_EXPORT_DIRECTORY *)(base + rva);
    DWORD *names  = (DWORD *)(base + exp->AddressOfNames);
    WORD  *ords   = (WORD  *)(base + exp->AddressOfNameOrdinals);
    DWORD *funcs  = (DWORD *)(base + exp->AddressOfFunctions);
    DWORD i, k;
    /* 命名导出: rva + 名字; FWD 导出解析转发串 "TARGET.Func"/"TARGET.#ord" */
    char tgtmod[MAX_PATH] = "";
    for (i = 0; i < exp->NumberOfNames; i++) {
        const char *nm = (const char *)(base + names[i]);
        DWORD frva = funcs[ords[i]];
        DWORD sz = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].Size;
        int fwd = (frva >= rva && frva < rva + sz);
        tgtmod[0] = 0;
        if (fwd) {
            const char *fs = (const char *)(base + frva);   /* "NTDLL.RtlFoo" */
            const char *dot = strchr(fs, '.');
            if (dot && dot > fs && dot - fs < (ptrdiff_t)sizeof tgtmod) {
                memcpy(tgtmod, fs, dot - fs);
                tgtmod[dot - fs] = 0;
                for (char *q = tgtmod; *q; q++) *q = tolower((unsigned char)*q);
                fprintf(f, "%s\t%08lX\t%s\t%d\tFWD\t%s\t%s\n", modname,
                        (unsigned long)frva, nm, (int)ords[i] + (int)exp->Base,
                        tgtmod, dot + 1);
                continue;
            }
        }
        fprintf(f, "%s\t%08lX\t%s\t%d\t%s\n", modname,
                (unsigned long)frva, nm, (int)ords[i] + (int)exp->Base,
                fwd ? "FWD" : "");
    }
    /* 序号导出: 枚举 AddressOfFunctions 中未被任何名字覆盖的槽位 */
    {
        BYTE *covered = (BYTE *)calloc(exp->NumberOfFunctions + 1, 1);
        if (covered) {
            for (i = 0; i < exp->NumberOfNames; i++)
                if (ords[i] < exp->NumberOfFunctions) covered[ords[i]] = 1;
            for (k = 0; k < exp->NumberOfFunctions; k++) {
                if (!covered[k] && funcs[k]) {
                    DWORD sz2 = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].Size;
                    int fwd2 = (funcs[k] >= rva && funcs[k] < rva + sz2);
                    fprintf(f, "%s\t%08lX\t#%lu\t%lu\t%s\n", modname,
                            (unsigned long)funcs[k], (unsigned long)(k + exp->Base),
                            (unsigned long)(k + exp->Base), fwd2 ? "FWD" : "");
                }
            }
            free(covered);
        }
    }
}

/* 沿 x86 跳转链解析 shim 蹦床, 返回最终非 shim 目标 / follow trampoline hops */
static DWORD follow_jumps(BYTE *p, DWORD apphelp_base, DWORD apphelp_end, int depth)
{
    if (depth > 8) return (DWORD)(DWORD_PTR)p;
    DWORD a = (DWORD)(DWORD_PTR)p;
    if (a >= apphelp_base && a < apphelp_end) {
        /* 仍在 shim 模块内, 继续解析跳转 */
        if (p[0] == 0xE9) {  /* jmp rel32 */
            DWORD t = a + 5 + *(int *)(p + 1);
            return follow_jumps((BYTE *)(DWORD_PTR)t, apphelp_base, apphelp_end, depth + 1);
        }
        if (p[0] == 0xEB) {  /* jmp rel8 */
            DWORD t = a + 2 + *(signed char *)(p + 1);
            return follow_jumps((BYTE *)(DWORD_PTR)t, apphelp_base, apphelp_end, depth + 1);
        }
        if (p[0] == 0xFF && p[1] == 0x25) {  /* jmp dword ptr [abs] */
            DWORD slot = *(DWORD *)(p + 2);
            DWORD t = *(DWORD *)(DWORD_PTR)slot;
            return follow_jumps((BYTE *)(DWORD_PTR)t, apphelp_base, apphelp_end, depth + 1);
        }
        if (p[0] == 0x68 && p[5] == 0xC3) {  /* push imm32; ret */
            DWORD t = *(DWORD *)(p + 1);
            return follow_jumps((BYTE *)(DWORD_PTR)t, apphelp_base, apphelp_end, depth + 1);
        }
    }
    return a;   /* 已离开 shim 模块(或无法解析), 返回当前地址 */
}

/* 在模块导出表里查某地址对应的导出名 / reverse lookup export by address */
static int lookup_export_by_addr(HMODULE *mods, int nmod, DWORD addr,
                                 char *outmod, char *outname)
{
    for (int i = 0; i < nmod; i++) {
        BYTE *b = (BYTE *)mods[i];
        IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)b;
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) continue;
        IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(b + dos->e_lfanew);
        DWORD rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
        DWORD szd = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].Size;
        if (!rva) continue;
        IMAGE_EXPORT_DIRECTORY *e = (IMAGE_EXPORT_DIRECTORY *)(b + rva);
        DWORD *funcs = (DWORD *)(b + e->AddressOfFunctions);
        DWORD *names = (DWORD *)(b + e->AddressOfNames);
        WORD  *ords  = (WORD  *)(b + e->AddressOfNameOrdinals);
        for (DWORD k = 0; k < e->NumberOfFunctions; k++) {
            if (funcs[k] && (DWORD)(DWORD_PTR)b + funcs[k] == addr) {
                for (DWORD j = 0; j < e->NumberOfNames; j++)
                    if (ords[j] == k) {
                        GetModuleFileNameA(mods[i], outmod, MAX_PATH);
                        lstrcpynA(outname, (const char *)(b + names[j]), 200);
                        return 1;
                    }
                /* 无名导出: 序号 */
                GetModuleFileNameA(mods[i], outmod, MAX_PATH);
                sprintf(outname, "#%lu", (unsigned long)(k + e->Base));
                return 1;
            }
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: unpacker.exe <packed.dll> <outdir>\n");
        return 2;
    }
    const char *dll = argv[1], *outdir = argv[2];

    char path[_MAX_PATH];
    sprintf(path, "%s\\dump.bin", outdir);
    FILE *fdump = fopen(path, "wb");
    sprintf(path, "%s\\header.txt", outdir);
    FILE *fhdr = fopen(path, "w");
    sprintf(path, "%s\\exports.txt", outdir);
    FILE *fexp = fopen(path, "w");
    sprintf(path, "%s\\modules.txt", outdir);
    FILE *fmod = fopen(path, "w");
    sprintf(path, "%s\\modmaps.txt", outdir);
    FILE *fmap = fopen(path, "w");
    if (!fdump || !fhdr || !fexp || !fmod || !fmap) {
        fprintf(stderr, "cannot open outputs in %s\n", outdir);
        return 2;
    }

    HMODULE h = LoadLibraryA(dll);
    if (!h) {
        fmt_err("LoadLibrary");
        fprintf(stderr,
            "LoadLibrary 失败 — 原始 DllMain 可能返回 FALSE 或需要宿主环境。\n");
        return 1;
    }
    printf("loaded at base %p\n", (void *)h);

    BYTE *base = (BYTE *)h;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    DWORD imgsize = nt->OptionalHeader.SizeOfImage;

    /* ---- 1. dump 内存镜像 (只复制已提交可读页) ---- */
    BYTE *buf = (BYTE *)calloc(imgsize, 1);
    if (!buf) return 2;
    DWORD off = 0;
    while (off < imgsize) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(base + off, &mbi, sizeof mbi)) break;
        DWORD region = (DWORD)mbi.RegionSize;
        DWORD end = off + region;
        if (mbi.State == MEM_COMMIT &&
            (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ |
                            PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY))) {
            DWORD take = (end > imgsize ? imgsize : end) - off;
            memcpy(buf + off, base + off, take);
        }
        if (end <= off) break;
        off = end;
    }
    fwrite(buf, 1, imgsize, fdump);
    fclose(fdump);
    printf("dumped %lu bytes\n", (unsigned long)imgsize);

    /* ---- 2. 头部信息 ---- */
    fprintf(fhdr, "base=%08lX SizeOfImage=%lx entryRVA=%08lx\n",
            (unsigned long)(DWORD_PTR)h, (unsigned long)imgsize,
            (unsigned long)nt->OptionalHeader.AddressOfEntryPoint);
    fprintf(fhdr, "DataDirectory[1] import RVA=%08lx size=%lx\n",
            (unsigned long)nt->OptionalHeader.DataDirectory[1].VirtualAddress,
            (unsigned long)nt->OptionalHeader.DataDirectory[1].Size);
    fprintf(fhdr, "sections=%d\n", (int)nt->FileHeader.NumberOfSections);
    IMAGE_SECTION_HEADER *sh = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < (int)nt->FileHeader.NumberOfSections; i++) {
        char nm[9]; memcpy(nm, sh[i].Name, 8); nm[8] = 0;
        fprintf(fhdr, "sec[%d] %-8s VA=%08lx VS=%08lx chars=%08lx\n", i, nm,
                (unsigned long)sh[i].VirtualAddress,
                (unsigned long)sh[i].Misc.VirtualSize,
                (unsigned long)sh[i].Characteristics);
    }
    fclose(fhdr);

    /* ---- 3. 目标 DLL 导出表 (内存中已恢复) ---- */
    dump_one_module_exports(h, "TARGET", fexp);
    fclose(fexp);

    /* ---- 4. 模块清单 + 每模块导出映射 ---- */
    HMODULE mods[256];
    int n = collect_modules(mods, 256);
    for (int i = 0; i < n; i++) {
        char mn[MAX_PATH];
        GetModuleFileNameA(mods[i], mn, MAX_PATH);
        IMAGE_NT_HEADERS *mnt = (IMAGE_NT_HEADERS *)
            ((BYTE *)mods[i] + ((IMAGE_DOS_HEADER *)mods[i])->e_lfanew);
        fprintf(fmod, "%08lX\t%08lx\t%s\n",
                (unsigned long)(DWORD_PTR)mods[i],
                (unsigned long)mnt->OptionalHeader.SizeOfImage, mn);
        dump_one_module_exports(mods[i], mn, fmap);
    }

    /* ---- 5. shim 蹦床修正: 找 apphelp, 扫 dump 内指向它的指针并解析真身 ---- */
    DWORD ah_base = 0, ah_end = 0;
    for (int i = 0; i < n; i++) {
        char mn[MAX_PATH];
        GetModuleFileNameA(mods[i], mn, MAX_PATH);
        if (strstr(_strlwr(mn), "apphelp.dll")) {
            IMAGE_NT_HEADERS *mnt = (IMAGE_NT_HEADERS *)
                ((BYTE *)mods[i] + ((IMAGE_DOS_HEADER *)mods[i])->e_lfanew);
            ah_base = (DWORD)(DWORD_PTR)mods[i];
            ah_end = ah_base + mnt->OptionalHeader.SizeOfImage;
            break;
        }
    }
    if (ah_base) {
        char path[_MAX_PATH];
        sprintf(path, "%s\\trampfix.txt", outdir);
        FILE *ffix = fopen(path, "w");
        if (ffix) {
            /* 解析 apphelp 自身导入表: FT槽绝对地址 -> 函数名 */
            IMAGE_DOS_HEADER *ados = (IMAGE_DOS_HEADER *)(DWORD_PTR)ah_base;
            IMAGE_NT_HEADERS *ant = (IMAGE_NT_HEADERS *)((BYTE *)ados + ados->e_lfanew);
            DWORD airva = ant->OptionalHeader.DataDirectory[1].VirtualAddress;
            #define AH_MAXIMP 4096
            static DWORD ahSlot[AH_MAXIMP]; static char ahName[AH_MAXIMP][128];
            static char ahMod[AH_MAXIMP][64];
            int ahN = 0;
            if (airva && airva < ant->OptionalHeader.SizeOfImage) {
                IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)((BYTE *)ados + airva);
                DWORD ahImg = ant->OptionalHeader.SizeOfImage;
                for (; d->Name && ahN < AH_MAXIMP - 64; d++) {
                    if (d->Name >= ahImg) break;
                    const char *dn = (const char *)((BYTE *)ados + d->Name);
                    DWORD oftr = d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk;
                    if (oftr >= ahImg || d->FirstThunk >= ahImg) continue;
                    DWORD *thunk = (DWORD *)((BYTE *)ados + oftr);
                    DWORD *ft   = (DWORD *)((BYTE *)ados + d->FirstThunk);
                    for (int q = 0; thunk[q] && ahN < AH_MAXIMP; q++) {
                        const char *fn;
                        char fbuf[128];
                        if (thunk[q] & 0x80000000) {
                            sprintf(fbuf, "#%u", (unsigned)(thunk[q] & 0xffff));
                            fn = fbuf;
                        } else {
                            if (thunk[q] + 2 >= ahImg) break;
                            fn = (const char *)((BYTE *)ados + thunk[q] + 2);
                        }
                        ahSlot[ahN] = (DWORD)(DWORD_PTR)&ft[q];
                        lstrcpynA(ahName[ahN], fn, 128);
                        lstrcpynA(ahMod[ahN], dn, 64);
                        ahN++;
                    }
                }
            }
            fprintf(ffix, ";apphelp imports: %d\n", ahN);
            for (DWORD off = 0x1000; off + 4 <= imgsize; off += 4) {
                DWORD v = *(DWORD *)(buf + off);
                if (v <= ah_base || v >= ah_end) continue;
                BYTE *w = (BYTE *)(DWORD_PTR)v;
                /* shim 包装函数特征: 8B FF 55 8B EC (mov edi,edi;push ebp;mov ebp,esp) */
                if (!(w[0] == 0x8B && w[1] == 0xFF && w[2] == 0x55 && w[3] == 0x8B && w[4] == 0xEC))
                    continue;
                fprintf(ffix, ";WRAP %08lX rva=%08lx calls:", (unsigned long)v,
                        (unsigned long)off);
                for (int q = 5; q < 128 && w[q]; q++) {
                    if (w[q] == 0xFF && w[q + 1] == 0x15) {
                        DWORD t = *(DWORD *)(w + q + 2);
                        for (int z = 0; z < ahN; z++)
                            if (ahSlot[z] == t) {
                                fprintf(ffix, " %s!%s", ahMod[z], ahName[z]);
                                break;
                            }
                        q += 5;
                    }
                }
                fprintf(ffix, "\n");
            }
            fclose(ffix);
            printf("trampfix.txt written (apphelp %08lx-%08lx, %d imports)\n",
                   (unsigned long)ah_base, (unsigned long)ah_end, ahN);
        }
    }

    fclose(fmod);
    fclose(fmap);
    printf("done. outputs in %s\n", outdir);
    return 0;
}
