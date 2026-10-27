"""
RM2K / RM2K3 引擎重建 —— Delphi 结构恢复扫描器 (M1 工具)

用途：
  1. 节表与镜像映射（VA <-> 文件偏移）
  2. 从 PACKAGEINFO 资源按原顺序提取编译单元清单（= 原版源码模块划分）
  3. 扫描 Delphi VMT（vmtSelfPtr 自指特征），恢复：
       类名 / 实例大小 / 父类 / 公布方法表(published methods)
  4. 解出 DFM 窗体资源里的对象树

VMT 布局依据 Delphi 2..7 (32-bit)：
     vmtSelfPtr    = -76   指向 VMT 自身（即 VS-76 处 dword == VS）
     vmtMethodTable= -52   published 方法表
     vmtClassName  = -44   指向 ShortString 类名
     vmtInstanceSize=-40
     vmtParent     = -36

published 方法表格式：
     Word Count
     Count * { Word SlotSize; Word Offset(相对 VMT); ShortString Name }

用法：
     py -3 tools/rm2k_delphi_scan.py <exe> [--json out.json] [--samples N]
"""
import struct
import sys
import json
import re

IMAGE_BASE = 0x400000


class Image:
    def __init__(self, path):
        self.path = path
        self.data = open(path, 'rb').read()
        d = self.data
        e = struct.unpack_from('<I', d, 0x3c)[0]
        self.pe_off = e
        machine, nsec = struct.unpack_from('<HH', d, e + 4)
        optsz = struct.unpack_from('<H', d, e + 20)[0]
        ts = struct.unpack_from('<I', d, e + 8)[0]
        opt = e + 24
        self.magic = struct.unpack_from('<H', d, opt)[0]
        self.entry = struct.unpack_from('<I', d, opt + 16)[0]
        self.image_base = struct.unpack_from('<I', d, opt + 28)[0]
        self.timestamp = ts
        dd = opt + (96 if self.magic == 0x10b else 112)
        self.dirs = [struct.unpack_from('<II', d, dd + i * 8) for i in range(16)]
        self.sections = []
        so = opt + optsz
        for i in range(nsec):
            nm, vs, va, rs, ro = struct.unpack_from('<8sIIII', d, so + i * 40)
            ch = struct.unpack_from('<I', d, so + i * 40 + 36)[0]
            self.sections.append({
                'name': nm.rstrip(b'\0').decode('latin1'),
                'va': va, 'vsize': vs, 'raw': ro, 'rawsize': rs, 'chars': ch,
            })
        assert machine == 0x14c, 'expect i386'

    # --- address helpers -------------------------------------------------
    def rva2off(self, rva):
        for s in self.sections:
            span = max(s['vsize'], s['rawsize'])
            if s['va'] <= rva < s['va'] + span and s['rawsize']:
                delta = rva - s['va']
                if delta < s['rawsize']:
                    return s['raw'] + delta
        return None

    def va2off(self, va):
        return self.rva2off(va - self.image_base)

    def read(self, va, n):
        o = self.va2off(va)
        if o is None:
            return None
        return self.data[o:o + n]

    def read_u32(self, va):
        b = self.read(va, 4)
        if b is None or len(b) < 4:
            return None
        return struct.unpack('<I', b)[0]

    def read_u16(self, va):
        b = self.read(va, 2)
        if b is None or len(b) < 2:
            return None
        return struct.unpack('<H', b)[0]

    def read_cstr(self, va, limit=64):
        o = self.va2off(va)
        if o is None:
            return None
        end = self.data.find(b'\0', o, o + limit)
        if end < 0:
            return None
        raw = self.data[o:end]
        try:
            return raw.decode('latin1')
        except Exception:
            return None

    def read_shortstring(self, va, limit=96):
        """Delphi ShortString: 首字节为长度。"""
        o = self.va2off(va)
        if o is None:
            return None
        ln = self.data[o]
        if ln == 0 or ln > limit:
            return None
        raw = self.data[o + 1:o + 1 + ln]
        if len(raw) != ln:
            return None
        if any(c < 0x20 or c > 0x7e for c in raw):
            return None
        return raw.decode('latin1')


def read_rcdata(img, base, sub):
    """从资源项指针读 RCDATA 字节。

    该指针可能直接是 data entry，也可能还是一层目录（语言子项）。
    """
    d = img.data

    def at(off):
        drva, dsz, cp, _ = struct.unpack_from('<IIII', d, base + off)
        fo = img.rva2off(drva)
        if fo is None:
            return None
        return d[fo:fo + dsz]

    if not (sub & 0x80000000):
        return at(sub)
    off = sub & 0x7fffffff
    n_named, n_id = struct.unpack_from('<HH', d, base + off + 12)
    for i in range(n_named + n_id):
        _, s2 = struct.unpack_from('<II', d, base + off + 16 + i * 8)
        if not (s2 & 0x80000000):
            return at(s2)
    return None


def extract_units(img):
    """从 PACKAGEINFO (RCDATA) 按原顺序提取单元名。"""
    rrva, rrsize = img.dirs[2]  # IMAGE_DIRECTORY_ENTRY_RESOURCE
    base = img.rva2off(rrva)
    if base is None:
        return []
    d = img.data

    def rd_name(off):
        ln = struct.unpack_from('<H', d, off)[0]
        return d[off + 2:off + 2 + ln * 2].decode('utf-16le')

    def entries(off):
        n_named, n_id = struct.unpack_from('<HH', d, off + 12)
        out = []
        for i in range(n_named + n_id):
            nm, sub = struct.unpack_from('<II', d, off + 16 + i * 8)
            lab = rd_name(base + (nm & 0x7fffffff)) if (nm & 0x80000000) else nm
            out.append((lab, sub))
        return out

    blob = None
    for lab, sub in entries(base):
        if lab != 10:  # RT_RCDATA
            continue
        if not (sub & 0x80000000):
            continue
        for lab2, sub2 in entries(base + (sub & 0x7fffffff)):
            if lab2 != 'PACKAGEINFO':
                continue
            blob = read_rcdata(img, base, sub2)
    if blob is None:
        return []

    # TPackageInfo 的单元名是 null 结尾 ASCII，按出现顺序去重
    units = []
    seen = set()
    for m in re.finditer(rb'[A-Za-z_][A-Za-z0-9_\.@]{1,60}', blob):
        s = m.group(0).decode('latin1')
        # 丢掉明显不是单元名的噪声（版本号、纯数字等），保留首字母大写的标识符
        if s in seen:
            continue
        seen.add(s)
        units.append(s)
    return units


def scan_vmts(img):
    """扫描 Delphi VMT。返回类信息列表。"""
    out = []
    seen_vs = set()
    for sec in img.sections:
        if not sec['rawsize']:
            continue
        start = sec['raw']
        end = sec['raw'] + sec['rawsize']
        for off in range(start, end - 4, 4):
            v = struct.unpack_from('<I', img.data, off)[0]
            va_here = sec['va'] + (off - sec['raw']) + img.image_base
            if v != va_here + 76:
                continue
            vs = va_here + 76
            if vs in seen_vs:
                continue
            name_ptr = img.read_u32(vs - 44)
            if not name_ptr:
                continue
            cname = img.read_shortstring(name_ptr)
            if not cname:
                continue
            seen_vs.add(vs)
            inst_size = img.read_u32(vs - 40)
            # vmtParent 指向一个 dword，该 dword 才是父类 VMT
            parent_ptr = img.read_u32(vs - 36)
            parent = None
            if parent_ptr:
                parent = img.read_u32(parent_ptr)
            methods = read_method_table(img, vs)
            out.append({
                'class': cname,
                'vmt': vs,
                'instance_size': inst_size,
                'parent': parent,
                'parent_name': None,  # 二阶段回填
                'methods': methods,
            })
    return out


def read_method_table(img, vs):
    """读取 published 方法表。

    布局: Word Count, 然后 Count 条 { Word Size; DWORD Address; ShortString Name }
    """
    mt = img.read_u32(vs - 52)
    if not mt:
        return []
    count = img.read_u16(mt)
    if not count or count > 512:
        return []
    res = []
    p = mt + 2
    for _ in range(count):
        size = img.read_u16(p)
        addr = img.read_u32(p + 2)
        if size is None or addr is None:
            break
        p += 6
        nm = img.read_shortstring(p)
        if nm is None:
            break
        res.append({'name': nm, 'size': size, 'address': addr})
        p += 1 + len(nm)
    return res


def dump_dfm(blob):
    """解二进制 DFM (TPF0)，返回 (类名, 属性列表, 子对象)。"""
    if blob[:4] != b'TPF0':
        return None
    pos = [4]

    def rb():
        v = blob[pos[0]]; pos[0] += 1; return v

    def rw():
        v = struct.unpack_from('<H', blob, pos[0])[0]; pos[0] += 2; return v

    def rdw():
        v = struct.unpack_from('<I', blob, pos[0])[0]; pos[0] += 4; return v

    def rstr():
        n = rb()
        if n == 0:
            n = rw()
            if n == 0:
                n = rdw()
        s = blob[pos[0]:pos[0] + n].decode('latin1'); pos[0] += n
        return s

    def rvalue():
        t = rb()
        if t == 0x00: return None
        if t == 0x01:
            vals = []
            while blob[pos[0]] != 0:
                vals.append(rvalue())
            pos[0] += 1
            return vals
        if t == 0x02:
            v = struct.unpack_from('<b', blob, pos[0])[0]; pos[0] += 1; return v
        if t == 0x03:
            v = struct.unpack_from('<h', blob, pos[0])[0]; pos[0] += 2; return v
        if t == 0x04:
            v = rdw(); return v - (1 << 32) if v >= (1 << 31) else v
        if t == 0x05:
            pos[0] += 10; return '<ext>'
        if t == 0x06: return rstr()
        if t == 0x07: return '#' + rstr()
        if t == 0x08: return False
        if t == 0x09: return True
        if t == 0x0A:
            n = rdw(); pos[0] += n; return '<bin %d>' % n
        if t == 0x0B:
            items = []
            while True:
                s = rstr()
                if not s: break
                items.append(s)
            return items
        if t == 0x0C:
            items = []
            while blob[pos[0]] != 0:
                if blob[pos[0]] in (0x02, 0x03, 0x04):
                    rvalue()
                props = []
                while blob[pos[0]] != 0:
                    pn = rstr(); props.append((pn, rvalue()))
                pos[0] += 1
                items.append(props)
            pos[0] += 1
            return items
        if t == 0x0D: pos[0] += 4; return '<single>'
        if t == 0x0E: pos[0] += 8; return '<cur>'
        if t == 0x0F: pos[0] += 8; return '<date>'
        if t == 0x10: return rstr()
        if t == 0x11:
            v = struct.unpack_from('<q', blob, pos[0])[0]; pos[0] += 8; return v
        if t == 0x12: return rstr()
        if t == 0x13: pos[0] += 8; return '<dbl>'
        return '<unk %#x>' % t

    def robj(depth, acc):
        cls = rstr(); name = rstr()
        props = []
        while blob[pos[0]] != 0:
            pn = rstr(); props.append((pn, rvalue()))
        pos[0] += 1
        node = {'class': cls, 'name': name, 'props': props, 'children': [], 'depth': depth}
        acc.append(node)
        while pos[0] < len(blob) and blob[pos[0]] == 0x01:
            pos[0] += 1
            robj(depth + 1, node['children'])

    acc = []
    try:
        robj(0, acc)
    except Exception:
        pass
    return acc


def extract_forms(img):
    """提取所有 DFM 资源（RCDATA 中 TPF0 开头的）。"""
    rrva, _ = img.dirs[2]
    base = img.rva2off(rrva)
    if base is None:
        return []
    d = img.data

    def rd_name(off):
        ln = struct.unpack_from('<H', d, off)[0]
        return d[off + 2:off + 2 + ln * 2].decode('utf-16le')

    def entries(off):
        n_named, n_id = struct.unpack_from('<HH', d, off + 12)
        out = []
        for i in range(n_named + n_id):
            nm, sub = struct.unpack_from('<II', d, off + 16 + i * 8)
            lab = rd_name(base + (nm & 0x7fffffff)) if (nm & 0x80000000) else nm
            out.append((lab, sub))
        return out

    forms = []
    for lab, sub in entries(base):
        if lab != 10 or not (sub & 0x80000000):
            continue
        for lab2, sub2 in entries(base + (sub & 0x7fffffff)):
            blob = read_rcdata(img, base, sub2)
            if blob is None or blob[:4] != b'TPF0':
                continue
            forms.append({'resource': lab2, 'size': len(blob), 'tree': dump_dfm(blob)})
    return forms


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return
    path = args[0]
    as_json = None
    samples = 20
    for i, a in enumerate(args):
        if a == '--json' and i + 1 < len(args):
            as_json = args[i + 1]
        if a == '--samples' and i + 1 < len(args):
            samples = int(args[i + 1])

    img = Image(path)
    print('=' * 78)
    print(path)
    print('  size=%d  timestamp=%d  entry=%#x  image_base=%#x' %
          (len(img.data), img.timestamp, img.entry, img.image_base))
    print('  sections:', ', '.join('%s(%#x)' % (s['name'], s['vsize']) for s in img.sections))

    units = extract_units(img)
    print('  PACKAGEINFO units: %d' % len(units))

    vmts = scan_vmts(img)
    by_vmt = {v['vmt']: v for v in vmts}
    for v in vmts:
        p = v['parent']
        if p in by_vmt:
            v['parent_name'] = by_vmt[p]['class']
    named = [v for v in vmts if v['parent_name']]
    with_m = [v for v in vmts if v['methods']]
    print('  VMTs: %d  (with resolvable parent: %d, with published methods: %d)' %
          (len(vmts), len(named), len(with_m)))

    forms = extract_forms(img)
    print('  DFM forms: %d' % len(forms))

    print('\n  --- 类清单（按实例大小降序，前 %d）---' % samples)
    for v in sorted(vmts, key=lambda x: -(x['instance_size'] or 0))[:samples]:
        print('    %-32s size=%6s parent=%-24s methods=%d  vmt=%#x' % (
            v['class'], v['instance_size'], v['parent_name'] or '-',
            len(v['methods']), v['vmt']))

    proj = [v for v in vmts if not is_vcl_class(v['class'])]
    print('\n  --- 非 VCL 类（%d 个，疑似游戏自身）---' % len(proj))
    for v in sorted(proj, key=lambda x: x['class']):
        print('    %-32s size=%6s parent=%-22s methods=%s' % (
            v['class'], v['instance_size'], v['parent_name'] or '-',
            ','.join(m['name'] for m in v['methods'][:6]) or '-'))

    for f in forms:
        print('\n  --- DFM %s ---' % f['resource'])
        def show(nodes, ind=4):
            for n in nodes:
                print(' ' * ind + '%s : %s' % (n['name'], n['class']))
                for k, val in n['props']:
                    sv = repr(val)
                    if len(sv) > 76:
                        sv = sv[:76] + '...'
                    print(' ' * (ind + 2) + '.%s = %s' % (k, sv))
                show(n['children'], ind + 2)
        show(f['tree'] or [])

    if as_json:
        payload = {
            'path': path,
            'size': len(img.data),
            'timestamp': img.timestamp,
            'entry': img.entry,
            'image_base': img.image_base,
            'sections': img.sections,
            'units': units,
            'classes': vmts,
            'forms': forms,
        }
        with open(as_json, 'w', encoding='utf-8') as fh:
            json.dump(payload, fh, indent=1, ensure_ascii=False)
        print('\n  JSON -> %s' % as_json)


VCL_PREFIX = (
    'TForm', 'TControl', 'TWinControl', 'TComponent', 'TPersistent', 'TObject',
    'TGraphic', 'TCanvas', 'TBitmap', 'TPicture', 'TStrings', 'TStream',
    'TButton', 'TLabel', 'TEdit', 'TPanel', 'TImage', 'TList', 'TMenu',
    'TMenuItem', 'TTimer', 'TScrollBox', 'TPaintBox', 'TComboBox', 'TCheckBox',
    'TRadioButton', 'TTabSheet', 'TPageControl', 'TStatusBar', 'TToolBar',
    'THeaderControl', 'TListView', 'TTreeView', 'TRichEdit', 'TMemo',
    'TStringList', 'TFileStream', 'TMemoryStream', 'TResourceStream',
    'TIniFile', 'TRegistry', 'TThread', 'TException', 'TGraphicControl',
    'TCustomForm', 'TScrollingWinControl', 'TCustomControl', 'TCommonDialog',
    'TOpenDialog', 'TSaveDialog', 'TFontDialog', 'TColorDialog', 'TPrinter',
    'TPrinterCanvas', 'TImageList', 'TAction', 'TActionList', 'TCollection',
    'TCollectionItem', 'TInterfacedObject', 'TInterfacedPersistent',
)


def is_vcl_class(name):
    return any(name.startswith(p) for p in VCL_PREFIX)


if __name__ == '__main__':
    main()
