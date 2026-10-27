/* dirsyn_test.c — B12/B13 dir 合成算法单元测试
 * 被测代码 = graphics_classes.c input_update 中逐指令还原的算法 (复制本)。
 * 期望值 = 按原版汇编 (0x10013cee-0x10013d9f) 手工推演。
 * 关键顺序: dir8 用原始 h/v (0x13d0c-0x13dcc) → 迟滞抑制 (0x13dcf) →
 *           dir4 末写覆盖 (0x13d65-0x10013d9f)。v = down - up。
 */
#include <stdio.h>
static int g_dir4, g_dir8, g_hyst;

static void synthesize(int up, int down, int left, int right)
{
    int v = down - up;
    int h = right - left;

    if (h < 0)      g_dir8 = (v < 0) ? 7 : (v == 0) ? 4 : 1;
    else if (h == 0) {
        g_dir8 = (v < 0) ? 8 : (v > 0) ? 2 : 0;
        if (v != 0) g_hyst = 0;         /* 0x10013d5f */
    } else {
        g_dir8 = (v < 0) ? 9 : (v == 0) ? 6 : 3;
    }

    if (h != 0) {
        if (v == 0) g_hyst = 1;         /* 0x10013deb */
        else if (g_hyst == 0) v = 0;    /* 0x10013ddd */
        else if (g_hyst == 1) h = 0;    /* 0x10013de4 */
    }

    g_dir4 = 0;
    if (h < 0) g_dir4 = 4;
    if (v < 0) g_dir4 = 8;
    if (h > 0) g_dir4 = 6;
    if (v > 0) g_dir4 = 2;
}

static int fails;
static void expect(const char *name, int u, int d, int l, int r, int hyst_in,
                   int e_dir8, int e_dir4, int e_hyst)
{
    g_hyst = hyst_in;
    synthesize(u, d, l, r);
    int ok = (g_dir8 == e_dir8 && g_dir4 == e_dir4 && g_hyst == e_hyst);
    printf("%-40s dir8=%d dir4=%d hyst=%d  %s\n", name, g_dir8, g_dir4, g_hyst,
           ok ? "OK" : "FAIL");
    if (!ok) { fails++; printf("   expected dir8=%d dir4=%d hyst=%d\n", e_dir8, e_dir4, e_hyst); }
}

int main(void)
{
    /* 直线移动 (确定迟滞轴) */
    expect("right (直线横)",           0,0,0,1, 0, 6, 6, 1);
    expect("left (直线横)",            0,0,1,0, 0, 4, 4, 1);
    expect("up (直线纵)",              1,0,0,0, 0, 8, 8, 0);
    expect("down (直线纵)",            0,1,0,0, 0, 2, 2, 0);
    /* B12: dir8 报原始对角, dir4 被迟滞归并为原轴 */
    expect("right 后 up (hyst=1 抑横)", 1,0,0,1, 1, 9, 8, 1);
    expect("right 后 down (hyst=1 抑横)",0,1,0,1,1, 3, 2, 1);
    expect("down 后 left (hyst=0 抑纵)", 0,1,1,0, 0, 1, 4, 0);
    expect("up 后 right (hyst=0 抑纵)",  1,0,0,1, 0, 9, 6, 0);
    expect("up+left (hyst=1 抑横)",     1,0,1,0, 1, 7, 8, 1);
    expect("down+right (hyst=0 抑纵)",  0,1,0,1, 0, 3, 6, 0);
    /* 直线移动刷新迟滞轴 */
    expect("up 后 down (刷新纵)",       0,1,0,0, 1, 2, 2, 0);
    expect("right 后 left (刷新横)",    0,0,1,0, 1, 4, 4, 1);
    /* 全松开 → 0, 迟滞保持 */
    expect("全松开",                    0,0,0,0, 1, 0, 0, 1);
    return fails ? 1 : 0;
}
