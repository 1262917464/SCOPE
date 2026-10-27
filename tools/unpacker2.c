/*
 * unpacker2.c — ASPack 脱壳 v2: 在 OEP 之前抓取原始(未执行 DllMain)内存镜像
 *
 * 原理:
 *   1. 复制壳 DLL, 扫 .aspack 节里的 "push OEP; ret" 交接指令, 把 imm32 改成
 *      固定地址 0x07000000 处的 shellcode (call [0x07000004]; ret)。
 *   2. shellcode 调本进程的 capture(): 此时壳已解压、IAT 已重建、原 DllMain 未跑,
 *      .data 为静态初值 → 拍到的才是"可重放"的镜像。
 *   3. capture 返回 0 → 壳 epilogue 以 FALSE 返回 → 加载器卸载, 本体无副作用。
 *
 * DllMain 上下文内不用 Toolhelp(会拿 loader lock), 改走 PEB 模块链。
 * 用法: unpacker2.exe <packed.dll> <outdir>
 */
#include <windows.h>
#include <wchar.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CAPTURE_BASE 0x07000000u

static char g_outdir[MAX_PATH];

/* ---------- PEB 模块枚举 (无 loader lock) ---------- */
typedef struct { DWORD base; DWORD size; char name[MAX_PATH]; } ModInfo;
static ModInfo g_mods[512]; static int g_nmods;

static void peb_collect_modules(void)
{
    DWORD peb;
    __asm__ __volatile__("movl %%fs:0x30, %0" : "=r"(peb));
    DWORD ldr = *(DWORD *)(peb + 0x0C);
    /* InMemoryOrderModuleList @ ldr+0x14, 链节点 = LDR_DATA_TABLE_ENTRY+0x08 */
    DWORD head = ldr + 0x14;
    DWORD cur = *(DWORD *)head;
    int guard = 0;
    g_nmods = 0;
    while (cur != head && guard++ < 200 && g_nmods < 512) {
        BYTE *ent = (BYTE *)(cur - 8);             /* InMemoryOrderLinks 偏移 0x08 */
        DWORD base = *(DWORD *)(ent + 0x18);
        DWORD size = *(DWORD *)(ent + 0x20);
        DWORD flen = *(DWORD *)(ent + 0x24);        /* FullDllName.Length */
        DWORD fbuf = *(DWORD *)(ent + 0x28);        /* FullDllName.Buffer */
        if (base && fbuf && flen < MAX_PATH * 2) {
            ModInfo *m = &g_mods[g_nmods];
            m->base = base; m->size = size;
            int j = 0;
            for (int i = 0; i + 1 < (int)flen && j < MAX_PATH - 1; i += 2)
                m->name[j++] = (char)((WCHAR *)fbuf)[i / 2 + 0];
            m->name[j] = 0;
            g_nmods++;
        }
        cur = *(DWORD *)cur;
    }
}

/* ---------- 导出表遍历 (与 v1 相同语义, 含 FWD 解析) ---------- */
static void dump_one_module_exports(BYTE *base, const char *modname, FILE *f)
{
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return;
    DWORD rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
    if (!rva) return;
    IMAGE_EXPORT_DIRECTORY *exp = (IMAGE_EXPORT_DIRECTORY *)(base + rva);
    DWORD *names = (DWORD *)(base + exp->AddressOfNames);
    WORD  *ords  = (WORD  *)(base + exp->AddressOfNameOrdinals);
    DWORD *funcs = (DWORD *)(base + exp->AddressOfFunctions);
    char tgtmod[MAX_PATH];
    for (DWORD i = 0; i < exp->NumberOfNames; i++) {
        const char *nm = (const char *)(base + names[i]);
        DWORD frva = funcs[ords[i]];
        DWORD sz = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].Size;
        int fwd = (frva >= rva && frva < rva + sz);
        tgtmod[0] = 0;
        if (fwd) {
            const char *fs = (const char *)(base + frva);
            const char *dot = strchr(fs, '.');
            if (dot && dot > fs && dot - fs < (ptrdiff_t)sizeof tgtmod) {
                memcpy(tgtmod, fs, dot - fs);
                tgtmod[dot - fs] = 0;
                for (char *q = tgtmod; *q; q++) *q = (char)tolower((unsigned char)*q);
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
    BYTE *covered = (BYTE *)calloc(exp->NumberOfFunctions + 1, 1);
    if (covered) {
        for (DWORD i = 0; i < exp->NumberOfNames; i++)
            if (ords[i] < exp->NumberOfFunctions) covered[ords[i]] = 1;
        for (DWORD k = 0; k < exp->NumberOfFunctions; k++) {
            if (!covered[k] && funcs[k]) {
                fprintf(f, "%s\t%08lX\t#%lu\t%lu\t\n", modname,
                        (unsigned long)funcs[k], (unsigned long)(k + exp->Base),
                        (unsigned long)(k + exp->Base));
            }
        }
        free(covered);
    }
}

/* ---------- 捕获: 在壳交接点被调, 全镜像落盘 ---------- */
static DWORD capture(DWORD a2, DWORD a3)
{
    (void)a2; (void)a3;
    BYTE *base = (BYTE *)0x10000000;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    DWORD imgsize = nt->OptionalHeader.SizeOfImage;

    char path[MAX_PATH];
    sprintf(path, "%s\\dump.bin", g_outdir);
    FILE *fdump = fopen(path, "wb");
    sprintf(path, "%s\\header.txt", g_outdir);
    FILE *fhdr = fopen(path, "w");
    sprintf(path, "%s\\exports.txt", g_outdir);
    FILE *fexp = fopen(path, "w");
    sprintf(path, "%s\\modules.txt", g_outdir);
    FILE *fmod = fopen(path, "w");
    sprintf(path, "%s\\modmaps.txt", g_outdir);
    FILE *fmap = fopen(path, "w");
    if (!fdump || !fhdr || !fexp || !fmod || !fmap) return 0;

    BYTE *buf = (BYTE *)calloc(imgsize, 1);
    if (!buf) return 0;
    DWORD off = 0;
    while (off < imgsize) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(base + off, &mbi, sizeof mbi)) break;
        DWORD end = off + (DWORD)mbi.RegionSize;
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

    fprintf(fhdr, "base=10000000 SizeOfImage=%lx entryRVA(stub)=%08lx OEP_pending\n",
            (unsigned long)imgsize,
            (unsigned long)nt->OptionalHeader.AddressOfEntryPoint);
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

    dump_one_module_exports(base, "TARGET", fexp);
    fclose(fexp);

    peb_collect_modules();
    for (int i = 0; i < g_nmods; i++) {
        fprintf(fmod, "%08lX\t%08lx\t%s\n", (unsigned long)g_mods[i].base,
                (unsigned long)g_mods[i].size, g_mods[i].name);
        dump_one_module_exports((BYTE *)(DWORD_PTR)g_mods[i].base,
                                g_mods[i].name, fmap);
    }
    fclose(fmod); fclose(fmap);
    free(buf);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: unpacker2.exe <packed.dll> <outdir>\n"); return 2; }
    strcpy(g_outdir, argv[2]);

    /* 固定地址 shellcode: call [0x07000004]; ret */
    BYTE *code = (BYTE *)VirtualAlloc((LPVOID)CAPTURE_BASE, 0x1000,
                                      MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (!code) { fprintf(stderr, "VirtualAlloc 0x07000000 failed\n"); return 2; }
    code[0] = 0xFF; code[1] = 0x15; *(DWORD *)(code + 2) = CAPTURE_BASE + 4;
    code[6] = 0xC3;
    *(DWORD *)(code + 4) = (DWORD)(DWORD_PTR)capture;

    /* 读壳 DLL, 找 .aspack 节里的 push+ret 交接, patch imm32 */
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror("open"); return 2; }
    fseek(f, 0, SEEK_END); long fsz = ftell(f); fseek(f, 0, SEEK_SET);
    BYTE *img = (BYTE *)malloc(fsz);
    fread(img, 1, fsz, f); fclose(f);

    DWORD pe = *(DWORD *)(img + 0x3c);
    WORD nsec = *(WORD *)(img + pe + 6);
    WORD optsz = *(WORD *)(img + pe + 20);
    IMAGE_SECTION_HEADER *sh = (IMAGE_SECTION_HEADER *)(img + pe + 24 + optsz);
    DWORD oepRva = *(DWORD *)(img + pe + 24 + 16);
    int patched = 0;
    for (int i = 0; i < nsec && !patched; i++) {
        BYTE *raw = img + sh[i].PointerToRawData;
        DWORD rsz = sh[i].SizeOfRawData;
        for (DWORD k = 0; k + 6 <= rsz; k++) {
            if (raw[k] == 0x68 && raw[k + 5] == 0xC3) {
                DWORD target = *(DWORD *)(raw + k + 1);
                DWORD rva = sh[i].VirtualAddress + k;
                /* 交接目标应落在原始代码区且不是壳区 */
                if (target >= 0x1000 && target < 0x200000 && rva >= 0x200000) {
                    printf("handoff @sec%d+%lx (rva %lx): OEP=%08lx -> patch to %08lX\n",
                           i, (unsigned long)k, (unsigned long)rva,
                           (unsigned long)target, (unsigned long)CAPTURE_BASE);
                    *(DWORD *)(raw + k + 1) = CAPTURE_BASE;
                    printf("OEP = 0x%08lx (rva %06lx)\n",
                           (unsigned long)(0x10000000 + target), (unsigned long)target);
                    patched = 1;
                    break;
                }
            }
        }
    }
    if (!patched) { fprintf(stderr, "push+ret handoff not found\n"); return 2; }

    /* 写临时副本并加载 — DllMain(壳stub)将解压后跳入 capture */
    char tmp[MAX_PATH];
    sprintf(tmp, "%s\\_patched.dll", g_outdir);
    f = fopen(tmp, "wb"); fwrite(img, 1, fsz, f); fclose(f);
    HMODULE h = LoadLibraryA(tmp);
    printf("LoadLibrary(patched)=%p (预期 NULL: capture 返回 FALSE)\n", (void *)h);
    WIN32_FIND_DATAA fd;
    sprintf(tmp, "%s\\dump.bin", g_outdir);
    if (FindFirstFileA(tmp, &fd) != INVALID_HANDLE_VALUE && fd.nFileSizeHigh == 0 && fd.nFileSizeLow > 0x100000) {
        printf("OK dump=%lu bytes\n", (unsigned long)fd.nFileSizeLow);
        return 0;
    }
    fprintf(stderr, "dump missing/too small\n");
    return 1;
}
