# RGSS3 M2 — 模块地图（RVA 区段 → 归属）
> 注：本文写于 2026-10-27 仓库重构前；文中 `backup/` `unpacked/` `build/` `test/` `third_party/` 等路径现位于 `local/` 下，`src/rgss_*.c` 现为 `src/runtimes/rgss2/*.c`，`tools/` 已按流水线分桶（unpack/pe/analysis/probe/verify）。


> 依据：RTTI 扫描（tools/rtti_scan.py：58 TypeDescriptor / 57 COL / 57 vftable）、
> 27 导出反编译（27/27）、特殊导入调用点聚类（ws2_32/Crypt*/shell32/控制台）、
> 第三方版本串。镜像 = unpacked/RGSS3_300_pristine（基址 0x10000000）。

## 1. 四层架构（M2 核心结论，与 202E 同构 + 视频层扩展）

```
┌─────────────────────────────────────────────────────────┐
│ RGSS API 层（27 导出, RVA 0x35C0–0x4700）                 │ Game.exe 契约
│   统一 eval 桥 sub_10030220 + rb_type 0x10004740          │
├─────────────────────────────────────────────────────────┤
│ CGameFrame 主控（0x26C 单例 @0x1025EB00；Run=0x10001620） │ 窗口/环境检查/双视口/
│   引擎初始化 0x1000CCA0（load_data/save_data/RGSS_VERSION）│ 启动画面/主循环 0x1000E9E0
│   └─ CRx* Ruby 绑定层: Sprite/Plane/Tilemap(+Sprite)/     │ 每个 = 一个 Ruby 内建类
│      Viewport/Window/Surface/Input/FontList/MemoryFile    │
├─────────────────────────────────────────────────────────┤
│ NxDrawLocal 2D 库: NxScreen/NxWindow/NxSprite/            │ GDI+DIB 渲染
│   NxSurfaceSprite/NxSurface/NxCustomDraw8/32/             │ 8/32bpp 双实现
│   NxDynamicDraw8/32/NxDIBImage(+BMP/PNG/JPEG Loader)      │ + 图片解码
├─────────────────────────────────────────────────────────┤
│ CSx* 媒体层: CSxAudio/CSxDirectSound(COM)/Segment/         │ DirectSound(COM) + MCI
│   SoundBuffer{Static,Stream}/Wave·AcmWav·Ogg Reader       │ WAV/ACM/Ogg 三路
│   + CMidiChecker                                          │
│ Theora 视频族: VideoManager/DataSource/Timer/Thread/       │ .ogv 影片播放
│   WorkerThread/AudioInterface(Factory)/Exception          │ (theoraplayer 库)
├─────────────────────────────────────────────────────────┤
│ Ruby 1.9.2p0 解释器（YARV + M17N 编码 + Onigmo            │ M3 官方源码替换
│   + socket 扩展 + win32/console/console-io 层）            │
└─────────────────────────────────────────────────────────┘
```

## 2. 类与 vftable（58 个；全表 unpacked/RGSS3_300_pristine/classes.txt）

| 层 | 类 | vftable RVA |
|----|----|-------------|
| 主控 | CGameFrame / CGameProperty | 0x1A2524 / 0x1A2614 |
| Rx | CRxFontList / CRxInput / CRxMemoryFile | 0x1A8AAC / 0x1A8AF0 / 0x1A8B14 |
| Rx | CRxPlane / CRxSprite / CRxSurface / CRxTilemap | 0x1A8B3C / 0x1A8B74 / 0x1A8D3C / 0x1A8D5C |
| Rx | CRxTilemapSprite / CRxViewport / CRxWindow | 0x1A91E4 / 0x1A9218 / 0x1A924C |
| Nx | CNxSurface / NxCustomDraw8 / NxCustomDraw32 | 0x22F134 / 0x22F14C / 0x22F19C |
| Nx | CNxScreen / CNxWindow / CNxFile | 0x22F24C / 0x22F304 / 0x22F350 |
| Nx | CNxSprite / CNxSurfaceSprite / NxDynamicDraw8 / 32 | 0x22F37C / 0x22F3CC / 0x22F140 / 0x23212C |
| Nx | CNxDIBImage / NxImageLoader / BMP·PNG·JPEG Loader | 0x23204C / 0x23258C / 0x233C70·0x233CA0·0x233CD8 |
| CSx | CSxAudio / CSxAudioSegment / CMidiChecker | 0x22F0F4 / 0x231FEC / 0x231FFC |
| CSx | CSxDirectSound / Static·Stream SoundBuffer / SxSoundBuffer | 0x232024 / 0x23311C·0x23319C / 0x23314C |
| CSx | Wave·AcmWav·Ogg FileReader | 0x233224 / 0x2331EC / 0x23327C |
| Theora | VideoManager / DataSource / U16FileDataSource | 0x232388 / 0x22F230 / 0x22F214 |
| Theora | CSxTheoraAudio×2 / Factory / TheoraAudioInterfaceFactory | 0x232304·0x232330 / 0x232350 / 0x232358 |
| Theora | TheoraTimer / AudioInterface / Thread / WorkerThread | 0x233358 / 0x233384 / 0x233394 / 0x2333A8 |
| std | exception 族 + type_info（CRT） | 0x1A79E8…0x245AA0 |

（Rx 层 vftable 集中在 .rdata 前段 0x1A8xxx–0x1A9xxx；Nx/CSx/Theora 集中在
0x22Fxxx–0x233xxx。）

## 3. 代码区段归属（按证据聚类）

| RVA 区段 | 归属 | 证据 |
|----------|------|------|
| 0x001000–0x0047xx | 引擎启动 + CGameFrame 方法 + RGSS 导出层 | 导出表；ctor 0x10001000、Run 0x10001620、WndProc 0x10001C80、错误弹框 0x10002050、引擎 init 0x1000CCA0 |
| 0x005xxx–0x00C5xx | RTP/路径/字体工具 + GetPath 解析 | sub_1000C580–0x1000DAB0（SetupRTP 助手族） |
| 0x00E9E0 | 真主循环入口（Run 尾部转入） | Run 反编译 |
| 0x010000–0x021xxx | 窗口/输入/视口/F12/消息（CGameFrame 协作者） | 0x1001EB0=F12 轮询、0x1001F60=MessageBox 包装、分辨率 clamp 0x10020F6、注册表 0x10001CB0 |
| 0x030220 / 0x036xxx–0x038xxx / 0x04740 / 0x048xxx / 0x054xxx–0x06Bxxx / 0x07Exxx | **Ruby 1.9.2 核心库**（eval/string/symbol/encoding/gc/funcall/raise/sprintf） | 导出桥引用的 rb_* 助手全部落此带 |
| 0x0816xx–0x0850xx | **Ruby socket 扩展 + win32 层** | ws2_32 全部 17 槽调用点（WSAStartup@0x8190A、select/recv/send/WSARecv…@0x82Fxx–0x84Exx）+ shell32 特殊文件夹 @0x8169D |
| 0x082Exx–0x084Fxx（交叠） | Ruby win32 控制台输入 | PeekConsoleInputA/ReadConsoleInputA 调用点 |
| 0x0B65xx | Ruby random.c（win32 种子） | CryptAcquireContext/GenRandom/Release 三连 @0xB65FE–0xB661F |
| 0x1850xx–0x197xxx | Ruby io/console + win32 console | SetConsoleCtrlHandler@0x1854F0、GetConsoleCP@0x187862、WriteConsole@0x196AF4/B4A |
| 0x10D000–0x110Fxx | Nx/CSx 类实现（图形/音频后端） | sub_1010DD70(NxScreen resize 544×416)、0x10109DA0(CSx ctor)、0x1010A360(音频 init)、0x1010E7A0/0x1010FC50 |
| 0x1A1000–0x1A2xxx（.rdata 首） | 原 IAT（已重建）+ 导入绑定区 | M1 |
| 0x1A2524–0x1A9xxx | Rx vftable + RTTI 集中 | rtti_scan |
| 0x22Fxxx–0x233xxx | Nx/CSx/Theora vftable + RTTI + zlib/png/jpeg/ogg/theora 常量 | rtti_scan + 版本串 |
| 0x2C6000–0x323000 (.vxdata) | 引擎大数据区（两版本零差异） | M1 diff |
| 0x324000 (.idata2) | 重建导入 | mkpe2 |

## 4. 第三方组件鉴定（M2 结案）

| 组件 | 版本/结论 | 证据 |
|------|------|------|
| zlib | **1.2.3** | "deflate 1.2.3"/"inflate 1.2.3" 版本串（与 202E 同版） |
| libpng | **1.2.16** | "libpng version "/"1.2.16" 串（与 202E 同版） |
| JPEG 解码器 | 内置（版本串待 M5 定名） | CNxJPEGImageLoader |
| Ogg/Vorbis | 内置解码器 | "OggS" 串 + CSxOggFileReader |
| Theora 视频 | **theoraplayer 库** | "theoraplayer"/"theora stream" 串 + Theora 类族（202E 无此层） |
| DirectSound | COM 动态创建（无 dsound 静态导入） | combase!CoCreateInstance/CoUninitialize + CSxDirectSound 类 |
| **ws2_32（17 导入）** | **Ruby 1.9 socket 扩展**（非 202E 的线程调度器依赖） | 调用点聚成完整 socket API 带 0x818xx–0x84Exx（send/recv/select/WSASend…），202E 仅 select×1 处的模式在 300 不成立 |
| advapi32 Crypt* ×3 | Ruby random.c 的 win32 随机种子 | 单函数三连调用 0xB65FE–0xB661F |
| shell32 ×3 | 特殊文件夹路径获取（Ruby win32 层） | SHGetSpecialFolderLocation 单点 0x8169D |
| 控制台 API 族 | Ruby win32/console-io 层 | 0x82Exx–0x84Fxx + 0x1850xx–0x197xxx 两带 |

## 5. RGSS3 相对 202E 的架构增量（M2 结论）

1. **Theora 视频层整体新增**（.ogv 影片；Project1 的 Movies/ 即其消费方）。
2. Ruby 1.8.1 → **1.9.2p0**：YARV/M17N/Onigmo 体量翻倍，socket 扩展全量编入。
3. 图形基准分辨率 544×416（202E 为 640×480 固定），窗口仍 640×480；
   `Graphics.resize_screen` 上限 640×480（汉化版值；修改版放开到 1024×768，见 M1）。
4. 错误报告全宽字符化；语言选择由 `GetUserDefaultUILanguage`（日语=+100 偏移）
   运行时决定（资源三语共存：英 13/14 组、日 19/20 组；汉化版另改）。
5. F12 = `GetKeyState(VK_F12)` → `rb_raise(RGSSReset)`（rb_raise 实现已还原 0x10026210）；
   F1 = 系统菜单项 0x7D1 → 属性对话框（6 个 RT_DIALOG）。

## 6. M2 遗留 → 后续里程碑

- sub_10019B70（音频就绪检查）精确语义 → M5。
- 系统目录关键文件名（Run 中 5×DWORD 拼接，错误 203"DirectX 8.0"）→ M5 读 0x101A2570。
- CoCreateInstance 的具体 CLSID（DSound or Theora or Shell）→ M5。
- 300↔301 函数级对应清单（Q9a 尾巴）：需对 301_v1 做同套 IDA 分析后按
  RTTI 类名+字节匹配，优先级低于 M3。
