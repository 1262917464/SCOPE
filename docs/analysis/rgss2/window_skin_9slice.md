# Window 9 宫格皮肤规格（逐像素实测）

> 提取自重构前根 `PROGRESS.md` §八（2026-10-27），为像素级实测规格，实现见 `src/runtimes/rgss2/graphics_classes.c`（Window 绘制）。
> 对比图：`docs/assets/window_9slice_compare.png`（左原版右重建）；运行时截图 `local/test/Project3_rebuild/window_compare.png`。

皮肤 `Graphics/System/Window.png` (128x128) 布局（逐像素测量 + 与原版截图对照验证）：

| 区域 | 内容 | 绘制方式 |
|------|------|---------|
| (0,0,64,64) | 背景：垂直渐变 (64,127,192)→(0,1,128) | **整块拉伸**（原版 104px 高内部渐变单调无 64px 周期 → 非平铺） |
| (0,64,64,64) | 条纹叠加：(0,0,64,alpha 40) 与透明交替 | 平铺（原版内部逐行 ±15 蓝通道摆动，2px 周期） |
| (64,0,64,64) | 边框：6px 边圈 黑/白/白/192灰/128灰/黑，圆角，内部透明，中心 (17,17)-(47,47) 有箭头 | 9 宫格，切角 6（恰好避开箭头） |
| (64,64,32,32) | 光标：1px 白 + 1px 蓝 + 内部 a32 | 9 宫格，切角 2 |

关键实测锚点（原版截图 x=186..192 与皮肤逐通道吻合）：
`黑,白,白,192,128,黑` 然后内部 → 证明边框区是 (64,0) 且边圈 6px。

坐标约定：contents 画在 (+16,+16)（`create_contents` 用 width-32/height-32），
并随 ox/oy 滚动；`cursor_rect` 是 **contents 相对坐标**（`item_rect` 用 contents.width），
故光标也要 +16,+16。
