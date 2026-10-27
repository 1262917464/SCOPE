/*
 * rm2k_delphi.c —— Delphi 运行时语义的 C 复刻
 */
#include "rm2k_delphi.h"

Rm2kGlobal g_rm2k;
uint32_t   g_rand_seed = 0;

/*
 * Delphi RandInt:
 *   RandSeed := RandSeed * $08088405 + 1
 *   Result := RandSeed        (无参 Random 返回 0..1 的浮点，用高位)
 * Random(n) = (RandSeed * n) 的 64 位高位
 *
 * 注意原版对 Random(0) 的处理：返回 0，且**仍然推进 RandSeed**。
 */
uint32_t delphi_random_next(void)
{
    g_rand_seed = g_rand_seed * 0x08088405u + 1u;
    return g_rand_seed;
}

int32_t delphi_random(int32_t range)
{
    uint64_t prod;

    if (range <= 0) {
        delphi_random_next();
        return 0;
    }
    delphi_random_next();
    prod = (uint64_t)g_rand_seed * (uint64_t)(uint32_t)range;
    return (int32_t)(prod >> 32);
}

/*
 * TAuroraDraw.Create 的等价物。
 * 对应 0x454D24：
 *   +0xAC := GetTickCount()
 *   +0xC4 := 4
 *   +0x30/+0x32/+0x48 := 1
 *   +0xC0 := 0x14 (20)
 *   +0xB8 := 1000.0 / frame_rate     (double)
 */
void rm2k_aurora_draw_create(AuroraDraw *self, uint32_t frame_rate)
{
    if (frame_rate == 0) {
        frame_rate = RM2K_DEFAULT_FRAME_RATE;
    }
    memset(self, 0, sizeof(*self));
    AURORA_FRAME_RATE(self)     = frame_rate;                    /* +0xC0 */
    AURORA_FRAME_SKIP_DIV(self) = 4;                             /* +0xC4 */
    AURORA_FRAME_INTERVAL(self) = 1000.0 / (double)frame_rate;   /* +0xB8 */
    AURORA_ACCUM(self)          = 0;                             /* +0xB4 */
    AURORA_LAST_TICK(self)      = 0;                             /* +0xB0，由 GetTickCount 填 */
}
