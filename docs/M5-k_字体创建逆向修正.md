# M5-k 字体创建与字体名选择逆向修正（地址标注版）

> 问题：重建版文字笔画明显比原版粗。
> 结论：**不是光栅化或 alpha 映射的问题，而是字体创建参数错了** ——
> 无条件执行的 `lfWidth` "两段式修正"让 GDI 把字形横向拉伸了。
> 修正后菜单文字近白像素比从 **1.258 降到 1.024**（原版=1.000）。

---

## 一、现象与量化

以 `test/diff_orig/f04_menu.png` 与原重建版的左侧菜单文字区域
(x 18..175, y 15..160) 统计近白像素 (R,G,B ≥ 200/200/180)：

| 版本 | 近白像素 | 相对原版 |
|------|---------|---------|
| 原版 | 1728 | 1.000 |
| 重建版（修前） | 2173 | **1.258** |
| 重建版（修后） | 1769 | **1.024** |

字高本身是对的（1x 截图实测两版文字行高均 15–16 px），
所以问题不是"字号大"，而是"同样字号下笔画更粗"。

---

## 二、根因一：lfWidth 被无条件计算（主因）

### 原版 0x129A0 的真实控制流

```
0x12A17  lstrcpynW(lfFaceName, names[idx], 32)
0x12A1D  lfHeight = [CFont+0x358]        ; Font#size
0x12A2D  lfWidth  = 0
0x12A37  CreateFontIndirectW(&lf)
0x12A55  fld1
0x12A5E  fcomp QWORD [CFont+0x360]       ; 字体缩放, 构造时置 1.0
0x12A64  fnstsw ax
0x12A66  test ah, 0x44
0x12A69  jnp  0x12ADA                    ; ← 相等 → 直接返回
--- 以下仅当缩放 != 1.0 ---
0x12A78  GetTextMetricsW(&tm)
0x12AA1  fild  [lfHeight]
0x12AA7  fimul tmAveCharWidth
0x12AAB  fidiv tmHeight
0x12AA1  fmul  [CFont+0x360]
0x12AB4  lfWidth = round(积)
0x12AB5  CreateFontIndirectW(&lf)        ; 重建
```

`0x12A69` 的 x87 判定：`fcomp` 恰好相等时 C3C2C0 = `0b100`，
`ah & 0x44` = `0x40`（单个 1，PF 为奇 → PF=0）→ **`jnp` 跳转**，
跳到 `0x12ADA` 直接 `pop ebp` 返回第一遍创建的字体，`lfWidth` 保持 0。

### 缩放字段 CFont+0x360 的取值

- `0x10012800-0x10012805`：构造函数 `fld1` → `fstp QWORD [esi+0x360]`，即 **1.0**
- `0x100128C0`：setter，先 `fcom [ecx+0x360]`，**相等就直接返回不改**（`jnp 0x100128E1`）
- 全库对 `0x360` 的写入只有这两处，三个 setter 调用点
  (`0x10003E81` / `0x100059CB` / `0x10005A1A`) 传的都是 `fld1` 出来的 1.0

→ **游戏全程缩放恒为 1.0，原版永远走"跳过修正、lfWidth=0"的路径。**

### 错误实现

```c
/* 修前：无条件算 lfWidth */
double v = (double)size * RGSS_FONT_SCALE * (double)tmAve / (double)tmH;
lf.lfWidth = (int)v;        /* size=22, tmAve=10, tmH=20 → 11 */
return CreateFontIndirectW(&lf);
```

`lfWidth = 11` 时 GDI 会把字形横向拉伸到 11 px，笔画随之变粗 ——
这就是那 25.8% 的白像素。

### 修正

```c
static HFONT rgss_create_font(const wchar_t *face, int size, int bold, int italic)
{
    LOGFONTW lf;
    memset(&lf, 0, sizeof lf);
    lf.lfHeight  = size;                  /* 0x12A1D */
    lf.lfWidth   = 0;                     /* 0x12A2D — 恒 0，不做修正 */
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = DEFAULT_QUALITY;
    if (bold)   lf.lfWeight = FW_BOLD;
    if (italic) lf.lfItalic = TRUE;
    lstrcpynW(lf.lfFaceName, face, LF_FACESIZE);
    return CreateFontIndirectW(&lf);      /* 0x12A37 即最终字体 */
}
```

---

## 三、根因二：lfQuality 用了 ANTIALIASED_QUALITY

「修前」实现里写了 `lf.lfQuality = ANTIALIASED_QUALITY`。

反汇编核查：`CFont+0x316`（LOGFONT 起点 `+0x2FC` 之后 0x1A 字节 = lfQuality 位）
**在整个 DLL 中从未被写入**。构造函数 `0x10012720` 用 `0x100dc6e0`
（memset）把 `+0x4` 起 0x280 字节清零，`lfQuality` 因此保持 **0 = DEFAULT_QUALITY**。

> 实测备注：`GetGlyphOutlineW(GGO_GRAY8_BITMAP)` 的灰度墨量与
> lfQuality 无关（q=0/2/4 三次测量 ink 完全相同），所以它不是本次
> "变粗"的原因，但仍是与原版不符的偏差，一并改回 DEFAULT_QUALITY。

---

## 四、根因三：字体名选择逻辑是臆造的（一并纠正）

### 4.1 之前的两处错误认知

1. 以为 `0x8F060` 是"字体名可用性过滤器"，`0x8EF40` 是"字体可用性检查"。
2. 于是用 `CreateFontW` + `GetTextFaceW` 回读比对来自造一个等价逻辑。

### 4.2 反汇编事实

`0x8EF40` 的实际内容：TLS (`TlsGetValue`/`TlsSetValue`)、临界区、
以及 `0x100FC2F0` 调用失败后检查 `GetLastError` 是否等于 `0x2736` / `0x276D`
（winsock 错误码 WSAENOTSOCK / WSAECONNRESET 一族）——
**这是防盗版联网校验，与字体毫无关系。**

### 4.3 真实的字体名链路

```
初始化
  0x10001238  call 0x10013450          ; 构造字体注册对象
  0x1000124A  [RGSS+0x260] = eax       ; 存进单例

0x13450  字体注册对象构造
  HDC = GetDC(NULL)                                  (0x1001347F)
  LOGFONT.lfCharSet = DEFAULT_CHARSET                (0x10013495: byte=1)
  EnumFontFamiliesExW(HDC, &lf, 0x10013330, this, 0)(0x100134A6)
  ReleaseDC                                          (0x100134AF)

0x13330  枚举回调
  取 ENUMLOGFONTEXW->lfFaceName (结构 +0x1C 起, 0x10013369)
  0x10013040 构造 wstring
  0x10012DA0 插入 std::set
  → 集合 = 系统真实可用的字体族名全集, 不做字符集/字形过滤

Font#name= 的 setter
  0x10007524  遍历 Ruby 侧名字数组
  0x10007555  call 0x10012FE0   ; 查集合 (std::set 有序查找)
  0x1000755A  test eax,eax / je → 不在集合就跳过
  0x1000755E  call 0x10012860   ; add_name

0x12860  add_name
  n = [this]; if (n >= 10) return;              ; 上限 10 个
  lstrcpynW(this + n*64 + 4, name, 32);         ; 每槽 64 字节
  [this] = n + 1; [this+0x36C] = 1;

0x129A0(idx)  用第 idx 个候选名建字体
  失败(返回 NULL) → 调用方 0x1604C 把 idx+1 重试, 最多 10 次
```

**关键点：没有任何"名字不存在就换个名字"的判断。换名完全由
`CreateFontIndirectW` 成功与否驱动。**

### 4.4 本项目实际的字体选择

脚本（`Data/Scripts.rvdata` 的 Main 段）设置：

```ruby
Font.default_name = ["霞鹜文楷等宽 TC Bold",
                     "霞鹜文楷 屏幕阅读版 R",
                     "LXGW WenKai Mono TC",
                     "DFKai-SB", "標楷體", "PMingLiU",
                     "新細明體", "Verdana", "Arial Unicode MS"]
Font.default_size = 22
```

用 `EnumFontFamiliesExW` 实测本机集合（共 5570 个族）：

| # | 名字 | 在系统集合中 |
|---|------|------------|
| 0 | 霞鹜文楷等宽 TC Bold | ✗ MISSING |
| 1 | 霞鹜文楷 屏幕阅读版 R | ✓ 命中（枚举序号 5319） |
| 2 | LXGW WenKai Mono TC | ✗ MISSING |
| 3 | DFKai-SB | ✓ |
| 4 | 標楷體 | ✗ |
| 5 | PMingLiU | ✓ |
| 6 | 新細明體 | ✗ |
| 7 | Verdana | ✓ |
| 8 | Arial Unicode MS | ✗ |

→ `[0]` 被过滤，`[1]` 是第一个命中的候选，**原版用的就是
"霞鹜文楷 屏幕阅读版 R"**。（项目自带的 `Fonts/kaiu.ttf` 提供的是
`DFKai-SB` 族，在本机属于系统已装字体，因此未被选中。）

> 探针踩坑记录：第一次枚举用固定 4000 槽缓冲区，结果正好 4000 且
> `[1]` 报 MISSING —— 那是**缓冲区被填满导致截断**，改成动态扩容后
> 枚举到 5570 个族，`[1]` 正确命中。教训：枚举类探针必须报告容量与
> 截断标志。

### 4.5 修正后的实现

- `font_build_face_set()` —— 对应 `0x13450`：`GetDC(NULL)` +
  `LOGFONT{lfCharSet=DEFAULT_CHARSET}` + `EnumFontFamiliesExW`
- `font_face_available()` —— 对应 `0x12FE0`：集合查找（线性等价）
- `font_collect_names()` —— 对应 setter + `0x12860`：按 Ruby 数组顺序
  收集命中集合的名字，**上限 10 个**
- `rgss_create_font_from_names()` —— 对应 `0x15FF7` 的索引重试：
  逐个 `CreateFontIndirectW`，成功即用

### 4.6 ⚠ 关键坑：集合容器被截断导致"字体选错"

第一版实现把集合容器写成了：

```c
static wchar_t g_face_set[RGSS_FONT_MAX_NAMES * 4][LF_FACESIZE];  /* = 40 个槽 */
```

而本机 `EnumFontFamiliesExW` 实际枚举出 **5570** 个字体族。集合被截断在 40 条，
于是 `"霞鹜文楷等宽 TC Bold"` 这种**真实不存在**的名字查不到 → 被判定"可用"
→ 直接交给 `CreateFontIndirectW` → GDI 匹配失败 → **静默回退到宋体**。

诊断日志（临时加在 draw_text 里）直接暴露了这一点：

```
[FACE] req=霞鹜文楷等宽 TC Bold -> actual=宋体 tmH=22 cellX(te)=22 bb=22x20
```

`req` 与 `actual` 不一致，`cellX=22`（宋体的全角步进）而非 17。

修正：容器扩到 `RGSS_FACE_SET_CAP = 16384`。修正后：

```
[FACE] req=霞鹜文楷 屏幕阅读版 R -> actual=霞鹜文楷 屏幕阅读版 R
       tmH=22 cellX(te)=17 bb=17x17
```

**教训**：枚举类探针/容器必须报告容量与截断状态；"查不到就放行"的
失败开放 (fail-open) 会让错误静默传播成完全不同的观感。


---

## 五、验证

`tools/difftest.exe` 按固定时序采集 6 帧，原版与重建版使用同一脚本。

| 帧 | 内容 |
|----|------|
| f00 | 标题 |
| f01 | 进场后地图 |
| f02 | Down×3 |
| f03 | Left×3 |
| f04 | Esc 开菜单 |
| f05 | Esc 关菜单 |

字体日志（`test/Project3_rebuild/rgss_text.log`）确认实际使用字体：

```
[draw] face=霞鹜文楷 屏幕阅读版 R size=22 color=(255,255,255,255) shadow=1
[draw] face=霞鹜文楷 屏幕阅读版 R size=22 color=(255,255,255,128) shadow=1
[draw] face=Arial size=32 color=(255,255,255,255) shadow=1
```

菜单文字区近白像素比 **1.258 → 1.024**，字高一致，笔画粗细肉眼已无差别。

### 5.1 字形对齐验证（最终）

字体集合修好后，逐列扫描"特殊技能"的字芯（近白列）：

| 项 | 原版 | 重建版 |
|----|------|--------|
| 字芯块起点 | 21, 38, 55, 60, 72, 81 | 21, 38, 55, 72, 81 |
| 字距 | 17 px | 17 px |
| 近白像素 | 1368 | 1405（×1.027） |

字距 17 px 与探针实测的"霞鹜文楷 屏幕阅读版 R" `gmCellIncX = 17` 完全一致，
字形起点逐块吻合。


---

## 六、剩余已知差异

1. **窗口背景多了菱形点阵**：原版菜单窗口背景是纯渐变，重建版叠了一层
   半透明菱形花纹。属于 `Window` 皮肤/背景合成路径，不是字体问题，待后续处理。
2. **背景渐变数值略有偏差**：同一坐标 `(20, 60)` 原版 `(36,78,151)`、
   重建版 `(60,94,160)`，疑似渐变端点或混合系数不同。
3. 文字区域仍有 `mean|diff| ≈ 14.6` 的逐像素差（不同字形的 kerning /
   亚像素相位差异），但整体墨量已对齐。

---

## 七、地址速查

| 地址 | 作用 |
|------|------|
| `0x10001238` | 构造字体注册对象 |
| `0x1000124A` | 存入 `[RGSS+0x260]` |
| `0x10012860` | `add_name`：追加候选名，上限 10 |
| `0x100128C0` | 缩放 setter（相等则不写） |
| `0x100129A0` | 按索引建字体（`lfWidth` 分支在此） |
| `0x10012FE0` | 查字体族集合 |
| `0x10013290` / `0x10013330` | 枚举/回调 |
| `0x10013450` | 字体注册对象构造（`EnumFontFamiliesExW`） |
| `0x10015FF7` | draw_text 内的索引重试循环 |
| `0x1001602C` | `GetGlyphOutlineW(GGO_GRAY8_BITMAP)` |
| `0x1001610A` | 灰度→alpha：`0x40→0xFF`，其余 `×4` |
| `0x1008EF40` | ⚠ 防盗版联网校验（**不是**字体检查） |
| `0x1008F060` | ⚠ 与字体无关（此前误判） |
