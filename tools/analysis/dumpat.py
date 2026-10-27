#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""dumpat.py <pid> <outdir> — 转储被调试器冻结在 OEP 的进程里的 RGSS202E 镜像"""
import ctypes, ctypes.wintypes as wt, struct, sys, os, string

k32 = ctypes.WinDLL('kernel32', use_last_error=True)
k32.ReadProcessMemory.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
k32.OpenProcess.argtypes = [wt.DWORD, wt.BOOL, wt.DWORD]
k32.OpenProcess.restype = wt.HANDLE
PROCESS_VM_READ = 0x10; PROCESS_QUERY_INFORMATION = 0x400
TH32CS_SNAPMODULE = 0x8
DLLBASE = 0x10000000

def rpm(hp, addr, size):
    buf = ctypes.create_string_buffer(size)
    got = ctypes.c_size_t()
    ok = k32.ReadProcessMemory(hp, ctypes.c_void_p(addr), buf, size, ctypes.byref(got))
    if not ok:
        return None
    return buf.raw[:got.value]

class MODULEENTRY32(ctypes.Structure):
    _fields_ = [('dwSize', wt.DWORD), ('th32ModuleID', wt.DWORD),
                ('th32ProcessID', wt.DWORD), ('GlblcntUsage', wt.DWORD),
                ('ProccntUsage', wt.DWORD), ('modBaseAddr', ctypes.c_void_p),
                ('modBaseSize', wt.DWORD), ('hModule', wt.HMODULE),
                ('szModule', ctypes.c_char * 256),
                ('szExePath', ctypes.c_char * 260)]

def main(pid, outdir):
    pid = int(pid)
    hp = k32.OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, False, pid)
    assert hp, 'OpenProcess failed'
    hdr = rpm(hp, DLLBASE, 0x400)
    assert hdr, 'read hdr failed'
    pe = struct.unpack_from('<I', hdr, 0x3c)[0]
    nth = rpm(hp, DLLBASE + pe, 0x100)
    nsec = struct.unpack_from('<H', nth, 6)[0]
    optsz = struct.unpack_from('<H', nth, 20)[0]
    imgsize = struct.unpack_from('<I', nth, 24 + 56)[0]
    print('imgsize=0x%x' % imgsize)

    # 分段读镜像(避免一次大读跨未提交页失败)
    img = bytearray()
    off = 0
    while off < imgsize:
        chunk = rpm(hp, DLLBASE + off, min(0x10000, imgsize - off))
        if chunk is None:
            # 找到该页所属区域边界, 跳过未提交部分
            img += b'\x00' * min(0x1000, imgsize - off)
            off += min(0x1000, imgsize - off)
        else:
            img += chunk
            off += len(chunk)
    open(os.path.join(outdir, 'dump.bin'), 'wb').write(img)

    with open(os.path.join(outdir, 'header.txt'), 'w') as f:
        f.write('base=%X SizeOfImage=%x\n' % (DLLBASE, imgsize))
        f.write('sections=%d\n' % nsec)
        so = 0x400 if False else None
        # 从 dump 里取节表
        secoff = pe + 24 + optsz
        for i in range(nsec):
            o = secoff + 40 * i
            nm = img[o:o+8].rstrip(b'\x00').decode('latin1')
            vs, va = struct.unpack_from('<II', img, o + 8)
            ch = struct.unpack_from('<I', img, o + 36)[0]
            f.write('sec[%d] %-8s VA=%08x VS=%08x chars=%08x\n' % (i, nm, va, vs, ch))

    # 模块快照
    snap = k32.CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid)
    me = MODULEENTRY32(); me.dwSize = ctypes.sizeof(MODULEENTRY32)
    mods = []
    if k32.Module32First(snap, ctypes.byref(me)):
        while True:
            mods.append((me.modBaseAddr or 0, me.modBaseSize, me.szExePath.decode('mbcs')))
            me2 = MODULEENTRY32(); me2.dwSize = ctypes.sizeof(MODULEENTRY32)
            ctypes.memmove(ctypes.byref(me2), ctypes.byref(me), ctypes.sizeof(MODULEENTRY32))
            if not k32.Module32Next(snap, ctypes.byref(me2)):
                break
            ctypes.memmove(ctypes.byref(me), ctypes.byref(me2), ctypes.sizeof(MODULEENTRY32))
    k32.CloseHandle(snap)
    print('modules: %d' % len(mods))

    def exports_of(base):
        """返回 [(func_rva, name_or_None, ordinal, fwd_string_or_None)]"""
        h = rpm(hp, base, 0x400)
        if not h: return None
        p = struct.unpack_from('<I', h, 0x3c)[0]
        n = rpm(hp, base + p, 0x100)
        if not n: return None
        edrva, edsz = struct.unpack_from('<II', n, 24 + 96)
        if not edrva: return None
        img_size = struct.unpack_from('<I', n, 24 + 56)[0]
        ed = rpm(hp, base + edrva, max(edsz, 0x28))
        if not ed: return None
        nfun, nname = struct.unpack_from('<II', ed, 0x14)
        if nfun > 0x20000 or nname > 0x20000: return None
        afuncs, anames, aords = struct.unpack_from('<III', ed, 0x1C)
        funcs = struct.unpack('<%dI' % nfun, rpm(hp, base + afuncs, 4 * nfun) or b'')
        names = struct.unpack('<%dI' % nname, rpm(hp, base + anames, 4 * nname) or b'')
        ords = struct.unpack('<%dH' % nname, rpm(hp, base + aords, 2 * nname) or b'')
        out = []
        def rstr(rva):
            for delta in range(0, 256, 16):
                s = rpm(hp, base + rva + delta, 128)
                if s:
                    e = s.find(b'\x00')
                    if e >= 0: return s[:e].decode('latin1', 'replace')
            return '?'
        for i in range(nname):
            frva = funcs[ords[i]] if ords[i] < nfun else 0
            fwd = None
            if edrva <= frva < edrva + edsz:
                fwd = rstr(frva)
            out.append((frva, rstr(names[i]), ords[i], fwd))
        cov = set(o for _, _, o, _ in out)
        for k in range(nfun):
            if k not in cov and funcs[k]:
                out.append((funcs[k], None, k, None))
        return out

    with open(os.path.join(outdir, 'modules.txt'), 'w') as fm, \
         open(os.path.join(outdir, 'modmaps.txt'), 'w') as fp, \
         open(os.path.join(outdir, 'exports.txt'), 'w') as fe:
        for base, size, path in mods:
            fm.write('%08X\t%08x\t%s\n' % (base, size, path))
            exp = exports_of(base)
            if not exp: continue
            tag = 'TARGET' if base == DLLBASE else path
            for frva, name, ordinal, fwd in exp:
                if fwd:
                    dot = fwd.find('.')
                    if dot > 0:
                        fp.write('%s\t%08X\t%s\t%d\tFWD\t%s\t%s\n'
                                 % (path, frva, name, ordinal + 1,
                                    fwd[:dot].lower(), fwd[dot+1:]))
                        if base == DLLBASE:
                            fe.write('%s\t%08X\t%s\t%d\tFWD\t%s\t%s\n'
                                     % (tag, frva, name, ordinal + 1,
                                        fwd[:dot].lower(), fwd[dot+1:]))
                        continue
                line = '%s\t%08X\t%s\t%d\t\n' % (path, frva, name or ('#%d' % (ordinal + 1)), ordinal + 1)
                fp.write(line)
                if base == DLLBASE: fe.write(line)
    print('done: dump.bin/modules.txt/modmaps.txt/exports.txt')

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
