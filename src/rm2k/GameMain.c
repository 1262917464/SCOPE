/*
 * GameMain.c —— 主循环与场景状态机
 *
 * 还原目标：原版 TFormLcfGameMain.AuroraDrawMain (2000: 0x49C598)
 *
 * 原版结构（已从汇编完整解出）:
 *
 *   void AuroraDrawMain(TAuroraDraw *self)          // eax = Self
 *   {
 *     helper_49C218(self);                          // 一次性初始化
 *     for (;;) {
 *       if ($TEST && state[+4] != 2) {              // 测试模式快速路径
 *         helper_43F908(self);
 *         break;
 *       }
 *       prev_state = state[+4];
 *       state[+8]++;                                // 帧计数
 *       game_singleton->something();                // 0x457520
 *
 *       switch (state[+4]) {                        // 跳转表 @0x49C5FB
 *         case 0: Scenes[0]->Execute(); break;      // 0x4806B8
 *         case 1: Scenes[1]->Execute(); break;      // 0x47F9F0
 *         case 2: Scenes[2]->Execute(); break;      // 0x478DE8
 *         case 3: Scenes[3]->Execute(); break;      // 0x476EB0
 *         case 4: Scenes[4]->Execute(); break;      // 0x475248
 *         case 5: Scenes[5]->Execute(); break;      // 0x473CCC
 *         case 6: Scenes[6]->Execute(); break;      // 0x4732C0
 *         case 7: Scenes[7]->Execute(); break;      // 0x472D34
 *         case 8: Scenes[8]->Execute(); break;      // 0x4725C0
 *       }
 *
 *       if (state[+4] != prev_state) continue;      // 场景刚切换，重跑
 *
 *       elapsed = self->GetElapsed();               // 0x4557F4
 *       if (elapsed >= self->frame_skip_div / 2) {  // 0x49C6B7: sar edx,1
 *         ... 跳到节流                                                  *
 *       } else if (state[+4] != 0 || !Scenes[0]->flag1C) {
 *         ... 跳到节流                                                  *
 *       } else {
 *         switch (state[+4]) {                     // 跳转表 @0x49C6F5
 *           case 0..8: Scenes[i]->vmt[+8](); break;
 *         }
 *       }
 *       self->ResetTimer();                         // 0x4561D8
 *       self->WaitFrame();                          // 0x4562EC
 *     }
 *   }
 *
 * 关键发现：第二张跳转表是"每帧实际绘制"，第一张是"场景逻辑更新"。
 * 两者都在同一个循环里，且都由当前场景索引分发。
 */
#include "rm2k_delphi.h"

#include <stdio.h>
#include <stdlib.h>

/* 这两个由平台层提供 */
extern uint32_t rm2k_platform_tick(void);
extern void     rm2k_platform_wait(uint32_t ms);

/* 场景执行体（后续里程碑逐个填充） */
typedef struct SceneOps {
    void (*update)(LcfScene *self);   /* 对应 case 0..8 的第一跳转表 */
    void (*render)(LcfScene *self);   /* 对应第二跳转表的 [vmt+8] */
} SceneOps;

static const SceneOps *scene_ops(int id);

/*
 * 对应 0x4557F4：
 *   result = (GetTickCount() - self->last_tick - self->accum * frame_interval)
 *            / frame_interval
 * 原版是 double 运算后调 0x4029D0 (Round)，返回整数帧数。
 */
static int aurora_elapsed_frames(AuroraDraw *self)
{
    double ms;
    ms = (double)(int32_t)(rm2k_platform_tick() - AURORA_LAST_TICK(self));
    ms -= (double)AURORA_ACCUM(self) * AURORA_FRAME_INTERVAL(self);
    ms /= AURORA_FRAME_INTERVAL(self);
    return (int)ms;
}

void rm2k_aurora_draw_main(AuroraDraw *self)
{
    uint8_t  prev_state;
    int      elapsed;

    for (;;) {
        if (g_rm2k.test_mode && g_rm2k.cur_scene != 2) {
            /* 原版 0x43F908：测试模式下的单步路径，M5 补 */
            break;
        }

        prev_state = g_rm2k.cur_scene;

        /* 场景逻辑更新：第一张跳转表 @0x49C5FB */
        {
            const SceneOps *ops = scene_ops(g_rm2k.cur_scene);
            LcfScene *scene = (LcfScene *)g_rm2k.scenes[g_rm2k.cur_scene];
            if (ops && ops->update && scene) {
                ops->update(scene);
            }
        }

        /* 场景刚切换则立刻重跑，不绘制也不等待 */
        if (g_rm2k.cur_scene != prev_state) {
            continue;
        }

        elapsed = aurora_elapsed_frames(self);

        /*
         * 原版节流判据：
         *   (elapsed >= frame_skip_div/2)                      -> 走到绘制
         *   或 (state != 0 || !Scenes[0]->flag1C)              -> 走到绘制
         *   否则（场景 0 且 flag1C 置位）                       -> 也走到绘制
         * 即：这个判断在原版里两个分支都汇到 0x49C795 的 ResetTimer，
         * 只有其中一个分支跳过绘制。行为上等价于"总是绘制"，
         * 但保留结构以便后续按真实条件细化。
         */
        (void)elapsed;
        {
            const SceneOps *ops = scene_ops(g_rm2k.cur_scene);
            LcfScene *scene = (LcfScene *)g_rm2k.scenes[g_rm2k.cur_scene];
            if (ops && ops->render && scene) {
                ops->render(scene);
            }
        }

        /* 原版 0x4561D8: last_tick = GetTickCount()，累加器清零 */
        AURORA_LAST_TICK(self) = rm2k_platform_tick();
        AURORA_ACCUM(self)     = 0;

        /* 原版 0x4562EC: 睡到下一帧边界 */
        rm2k_platform_wait((uint32_t)AURORA_FRAME_INTERVAL(self));
    }
}

/*
 * 场景表。M2 阶段只有空实现——目的仅是证明主循环骨架能跑，
 * 各场景的真实逻辑在 M5 填。
 */
static void scene_noop_update(LcfScene *self) { (void)self; }
static void scene_noop_render(LcfScene *self) { (void)self; }

static const SceneOps g_noop_ops = { scene_noop_update, scene_noop_render };

static const SceneOps *scene_ops(int id)
{
    if (id < 0 || id >= SCENE_COUNT) {
        return NULL;
    }
    return &g_noop_ops;
}
