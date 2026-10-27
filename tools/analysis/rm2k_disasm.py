"""
RM2K 引擎重建 —— 函数反汇编与结构分析器 (M2 工具)

用途：
  - 按 VA 精确反汇编 Delphi 函数（capstone，x86 32-bit）
  - 识别 Delphi 寄存器调用约定：eax=Self, edx/ecx=第 1/2 参数
  - 提取调用目标、字符串引用、全局变量引用
  - 输出类 C 伪代码骨架

用法：
  py -3 tools/rm2k_disasm.py <exe> <hex-va> [--len N] [--fn]
  py -3 tools/rm2k_disasm.py backup/2000.exe 0x49bcc0 --fn
"""
import struct
import sys
import re
from capstone import Cs, CS_ARCH_X86, CS_MODE_32, CS_OPT_SYNTAX_INTEL
import pefile

MD = Cs(CS_ARCH_X86, CS_MODE_32)
MD.detail = True
MD.syntax = CS_OPT_SYNTAX_INTEL


class Exe:
    def __init__(self, path):
        self.path = path
        self.pe = pefile.PE(path, fast_load=True)
        self.pe.parse_data_directories(directories=[
            pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_IMPORT'],
            pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_RESOURCE'],
        ])
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.data = self.pe.__data__
        self._build_iat()
        self._build_strings()

    # -- address helpers ------------------------------------------------
    def va2off(self, va):
        rva = va - self.base
        for s in self.pe.sections:
            if s.VirtualAddress <= rva < s.VirtualAddress + max(
                    s.Misc_VirtualSize, s.SizeOfRawData):
                d = rva - s.VirtualAddress
                if d < s.SizeOfRawData:
                    return s.PointerToRawData + d
        return None

    def read(self, va, n):
        o = self.va2off(va)
        if o is None:
            return None
        return self.data[o:o + n]

    def u32(self, va):
        b = self.read(va, 4)
        return struct.unpack('<I', b)[0] if b and len(b) == 4 else None

    def code(self, va, n):
        return self.read(va, n) or b''

    # -- import table ---------------------------------------------------
    def _build_iat(self):
        self.iat = {}
        if not hasattr(self.pe, 'DIRECTORY_ENTRY_IMPORT'):
            return
        for entry in self.pe.DIRECTORY_ENTRY_IMPORT:
            dll = entry.dll.decode('latin1')
            for imp in entry.imports:
                if imp.name:
                    self.iat[imp.address] = (dll, imp.name.decode('latin1'))

    def iat_name(self, va):
        return self.iat.get(va)

    # -- string table ---------------------------------------------------
    def _build_strings(self):
        """收录 Pascal ShortString（首字节长度）与 C 字符串。"""
        self.strings = {}
        d = self.data
        for s in self.pe.sections:
            if not s.SizeOfRawData:
                continue
            start = s.PointerToRawData
            end = start + s.SizeOfRawData
            va_off = s.VirtualAddress - s.PointerToRawData + self.base
            # C 字符串：连续的 0x20..0x7e 后接 0
            for m in re.finditer(rb'[\x20-\x7e]{4,}\x00', d[start:end]):
                va = va_off + start + m.start()
                self.strings[va] = ('cstr', m.group(0)[:-1].decode('latin1'))
            # Pascal ShortString：len 字节后接 len 个可打印字符
            for i in range(start, end - 2):
                n = d[i]
                if 3 <= n <= 120 and i + 1 + n <= end:
                    body = d[i + 1:i + 1 + n]
                    if all(0x20 <= c <= 0x7e for c in body):
                        # 前一个字节不该是长串的一部分
                        va = va_off + i
                        if va not in self.strings:
                            self.strings[va] = ('pstr', body.decode('latin1'))

    def str_at(self, va):
        return self.strings.get(va)


def fmt_bytes(b, n=12):
    return ' '.join('%02x' % x for x in b[:n])


def disasm(exe, va, length=400, stop_at_ret=True):
    raw = exe.code(va, length)
    out = []
    for ins in MD.disasm(raw, va):
        out.append(ins)
        if stop_at_ret and ins.mnemonic in ('ret', 'retn'):
            break
        if len(out) > 400:
            break
    return out


def analyze(exe, va, label='', show_all=False):
    ins_list = disasm(exe, va)
    print('=' * 78)
    print('%s  VA=%#x  (%d instructions)' % (label or '?', va, len(ins_list)))
    print('=' * 78)
    calls = []
    for ins in ins_list:
        ann = ''
        # direct call/jmp
        if ins.mnemonic in ('call', 'jmp') and ins.op_str:
            m = re.match(r'^(0x[0-9a-f]+)$', ins.op_str)
            if m:
                tgt = int(m.group(1), 16)
                nm = exe.str_at(tgt)
                sub = containing_class_method(exe, tgt)
                ann = '   ; -> %s' % (sub or hex(tgt))
                if ins.mnemonic == 'call':
                    calls.append(tgt)
            else:
                # indirect call through IAT: call dword ptr [0x...]
                m2 = re.search(r'\[(0x[0-9a-f]+)\]', ins.op_str)
                if m2:
                    slot = int(m2.group(1), 16)
                    imp = exe.iat_name(slot)
                    if imp:
                        ann = '   ; -> %s!%s' % imp
                        if ins.mnemonic == 'call':
                            calls.append(slot)
        # immediates that point at strings
        for m in re.finditer(r'0x([0-9a-f]{5,8})', ins.op_str):
            v = int(m.group(1), 16)
            s = exe.str_at(v)
            if s:
                ann += '   ; %s %r' % (s[0], s[1][:48])
                break
            imp = exe.iat_name(v)
            if imp and '-> ' not in ann:
                ann += '   ; %s!%s' % imp
        print('  %08x  %-28s %s' % (ins.address, ins.mnemonic + ' ' + ins.op_str, ann))
    return ins_list, calls


def containing_class_method(exe, va):
    """已知 published 方法的反向查表。"""
    table = getattr(exe, '_pubcache', None)
    if table is None:
        table = {}
        for cls, vs, methods in iter_published(exe):
            for m in methods:
                table[m['address']] = '%s.%s' % (cls, m['name'])
        exe._pubcache = table
    return table.get(va)


def iter_published(exe):
    """枚举所有 published 方法表。"""
    import rm2k_delphi_scan as ds
    img = ds.Image(exe.path)
    seen = set()
    for sec in img.sections:
        if not sec['rawsize']:
            continue
        for off in range(sec['raw'], sec['raw'] + sec['rawsize'] - 4, 4):
            v = struct.unpack_from('<I', img.data, off)[0]
            va = sec['va'] + (off - sec['raw']) + img.image_base
            if v != va + 76:
                continue
            vs = va + 76
            if vs in seen:
                continue
            np = img.read_u32(vs - 44)
            cn = img.read_shortstring(np) if np else None
            if not cn:
                continue
            seen.add(vs)
            ms = ds.read_method_table(img, vs)
            if ms:
                yield cn, vs, ms


def main():
    args = sys.argv[1:]
    if len(args) < 2:
        print(__doc__)
        return
    exe = Exe(args[0])
    va = int(args[1], 16)
    label = ''
    length = 400
    for i, a in enumerate(args):
        if a == '--label' and i + 1 < len(args):
            label = args[i + 1]
        if a == '--len' and i + 1 < len(args):
            length = int(args[i + 1])
    analyze(exe, va, label, length)


if __name__ == '__main__':
    main()
