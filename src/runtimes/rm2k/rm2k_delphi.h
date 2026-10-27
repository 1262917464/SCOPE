/*
 * rm2k_delphi.h —— Delphi 运行时语义的 C 复刻
 *
 * 对应 RM2K/2003 原版（Delphi 5/7，32-bit）的运行时约定：
 *   - Delphi 寄存器调用约定: eax=Self, edx=第1参, ecx=第2参
 *   - 对象布局: 偏移 0 是 VMT 指针，VMT 负偏移存类元信息
 *   - ShortString: 首字节长度 + 内容
 *   - 全局 RandSeed LCG（乘数 0x08088405）
 *
 * 本文件只提供"引擎需要的最小运行时"，不复刻整个 VCL。
 */
#ifndef RM2K_DELPHI_H
#define RM2K_DELPHI_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* 对象模型                                                            */
/* ------------------------------------------------------------------ */

/*
 * 原版每个对象的第 0 个 dword 是 VMT 指针。
 * VMT 布局（相对 VMT 的负偏移，与 Delphi 2..7 一致）：
 *   -76 vmtSelfPtr      指向自身
 *   -52 vmtMethodTable  published 方法表
 *   -44 vmtClassName    指向 ShortString 类名
 *   -40 vmtInstanceSize 实例字节数
 *   -36 vmtParent       指向"父类 VMT 指针"的指针（一层间接）
 */
typedef struct DelphiClass DelphiClass;

struct DelphiClass {
    int32_t     self_offset;        /* 占位，实际访问一律用 DelphiVmt() */
};

/* 对象头：原版对象的前 4 字节 */
typedef struct DObject {
    const void *vmt;
} DObject;

static inline const void *dobj_vmt(const void *obj)
{
    return *(const void *const *)obj;
}

/* ------------------------------------------------------------------ */
/* ShortString                                                         */
/* ------------------------------------------------------------------ */

/* Delphi ShortString: 最大 255 字节，+1 长度字节 */
typedef struct ShortString {
    uint8_t  len;
    char     data[256];
} ShortString;

/* ------------------------------------------------------------------ */
/* 全局状态（对应原版 DATA 段里的全局槽）                              */
/* ------------------------------------------------------------------ */

/*
 * 原版场景单例槽。三个版本的具体地址不同，但语义一致：
 *   CurScene  —— 当前场景索引（0..8）
 *   Scenes[i] —— 第 i 个场景对象指针（懒创建，缓存在 BSS）
 *
 * 帧循环靠这两个东西做状态机分发。
 */
enum Rm2kSceneId {
    SCENE_FIELD      = 0,
    SCENE_MENU       = 1,
    SCENE_BATTLE     = 2,
    SCENE_SHOP       = 3,
    SCENE_NAME       = 4,
    SCENE_SAVEFILE   = 5,
    SCENE_TITLE      = 6,
    SCENE_GAMEOVER   = 7,
    SCENE_DEBUG      = 8,
    SCENE_COUNT      = 9
};

typedef struct Rm2kGlobal {
    /* 当前场景 */
    uint8_t      cur_scene;

    /* 场景对象表，懒创建 */
    void        *scenes[SCENE_COUNT];

    /* $TEST / $BTEST / $TESTPLAY / $HIDE_TITLE 等命令行开关 */
    uint8_t      test_mode;       /* $TEST      */
    uint8_t      battle_test;     /* $BTEST     */
    uint8_t      test_play;       /* $TESTPLAY  */
    uint8_t      hide_title;      /* $HIDE_TITLE*/
    uint8_t      windowed;        /* 'Window'   */

    /* 帧计时 */
    uint32_t     frame_rate;      /* 默认 20 */
    uint32_t     last_tick;       /* ms */
    float        accum;           /* 原版用 double 累加 */
} Rm2kGlobal;

extern Rm2kGlobal g_rm2k;

/* ------------------------------------------------------------------ */
/* Delphi RandSeed LCG                                                 */
/* ------------------------------------------------------------------ */

/*
 * 原版用 Delphi 的 RandInt：
 *   RandSeed = RandSeed * 0x08088405 + 1     (32 位无符号回绕)
 *   Random(n) 返回 (RandSeed * n) >> 32 的高位
 * 已确认三个 exe 的 CODE 段都只有一处 0x08088405 立即数。
 */
extern uint32_t g_rand_seed;

uint32_t delphi_random_next(void);
int32_t  delphi_random(int32_t range);

/* ------------------------------------------------------------------ */
/* 场景基类                                                            */
/* ------------------------------------------------------------------ */

/*
 * TLcfScene 原版实例大小 12 字节：
 *   +0  vmt
 *   +4  ??? (子类扩到 +0xC 开始用)
 *   +8  ???
 * 子类从 +0xC 起加自己的字段。
 *
 * 观察到的一致模式（TLcfFieldScene/TLcfMenuScene/TLcfBattleScene 等）：
 *   +0xC  initialized 标志（bool）
 *   +0xD  update 标志
 */
typedef struct LcfScene {
    const void *vmt;              /* +0x00 */
    int32_t     field04;          /* +0x04 */
    int32_t     field08;          /* +0x08 */
    uint8_t     initialized;      /* +0x0C */
    uint8_t     flag0d;           /* +0x0D */
} LcfScene;

/* 场景虚表槽位（从 AuroraDrawMain 的 call [edx+8] 观察） */
#define VMT_EXECUTE  0x08         /* 每帧执行，主循环直接调这个 */

typedef void (*SceneExecuteFn)(LcfScene *self);

/* ------------------------------------------------------------------ */
/* TAuroraDraw —— 渲染组件                                             */
/* ------------------------------------------------------------------ */

/*
 * 原版 TAuroraDraw 实例 220 字节（2000/2003 均为 0xDC）。
 * 已确认的关键字段：
 *   +0x98  frame_callback   FormCreate 写入 AuroraDrawMain
 *   +0x9C  owner_form       FormCreate 写入窗体自身
 *   +0xAC  base_tick        GetTickCount() 基准（构造函数写入）
 *   +0xB0  last_tick        上次 tick (ms)
 *   +0xB4  accum            帧累加器（int）
 *   +0xB8  frame_interval   double，= 1000.0 / frame_rate
 *   +0xC0  frame_rate       默认 20
 *   +0xC4  frame_skip_div   默认 4
 *
 * 用字节数组 + 显式偏移访问，避免 i686 上 double 对齐差异改变布局。
 */
#define AURORA_INSTANCE_SIZE  0xDC

typedef struct AuroraDraw {
    uint8_t raw[AURORA_INSTANCE_SIZE];
} AuroraDraw;

/* 字段访问宏：完全对应原版 dword 偏移 */
#define AURORA_VMT(p)            (*(const void **)((p)->raw + 0x00))
#define AURORA_FRAME_CALLBACK(p) (*(void **)((p)->raw + 0x98))
#define AURORA_OWNER_FORM(p)     (*(void **)((p)->raw + 0x9C))
#define AURORA_BASE_TICK(p)      (*(uint32_t *)((p)->raw + 0xAC))
#define AURORA_LAST_TICK(p)      (*(uint32_t *)((p)->raw + 0xB0))
#define AURORA_ACCUM(p)          (*(int32_t *)((p)->raw + 0xB4))
#define AURORA_FRAME_INTERVAL(p) (*(double *)((p)->raw + 0xB8))
#define AURORA_FRAME_RATE(p)     (*(uint32_t *)((p)->raw + 0xC0))
#define AURORA_FRAME_SKIP_DIV(p) (*(uint32_t *)((p)->raw + 0xC4))

/* ------------------------------------------------------------------ */
/* 帧循环                                                              */
/* ------------------------------------------------------------------ */

/* 20 fps 默认帧率（构造函数里 mov dword [ebx+0xC0], 0x14） */
#define RM2K_DEFAULT_FRAME_RATE  20u
/* 窗口逻辑分辨率（构造函数里 edx=0x140, ecx=0xF0） */
#define RM2K_SCREEN_WIDTH        320
#define RM2K_SCREEN_HEIGHT       240

void rm2k_aurora_draw_create(AuroraDraw *self, uint32_t frame_rate);

#endif /* RM2K_DELPHI_H */
