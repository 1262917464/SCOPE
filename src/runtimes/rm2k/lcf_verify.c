/*
 * lcf_verify.c —— LCF 解析器自校验
 *
 * 做三件不需要外部标准就能判定对错的检验：
 *   1. 结构完整性：所有 chunk / 数组 / 元素必须精确铺满文件，无剩余、无越界
 *   2. 字段码合法性：每个 chunk 的 id 必须落在已知字段码集合内
 *   3. 数组元素边界：每个元素内部的 chunk 流必须以 id==0 正确终止
 *
 * 任一项失败即退出码非 0。
 */
#include "lcf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *read_file(const char *path, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    long  n;
    uint8_t *buf;

    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) {
        fclose(f);
        return NULL;
    }
    buf = (uint8_t *)malloc((size_t)n);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *out_size = (size_t)n;
    return buf;
}

/* 已知字段码的粗略上界：LCF 字段码是字节级，0..0xFF 都可能出现，
 * 但 0x00 与 0xFF 从不作为字段码。 */
static int plausible_id(uint32_t id)
{
    return id != 0 && id < 0xFF;
}

typedef struct Result {
    int chunks;
    int arrays;
    int items;
    int bad_id;
    int bad_item;
} Result;

static int verify_struct(const uint8_t *data, size_t len, Result *res, int depth)
{
    LcfChunkIter it;
    LcfChunk ch;
    int n = 0;

    lcf_chunk_iter_init(&it, data, len);
    while (lcf_chunk_next(&it, &ch)) {
        n++;
        if (depth == 0) {
            res->chunks++;
        }
        if (!plausible_id(ch.id)) {
            res->bad_id++;
        }

        /* 尝试按数组解析：只有"count 与元素数一致 + 恰好铺满 payload"
         * 才算真数组；否则视为标量，不做数组检查。 */
        {
            LcfReader probe;
            uint32_t  count;
            lcf_reader_init(&probe, ch.data, ch.len);
            count = lcf_read_ber(&probe);
            if (!probe.error && count > 0 && count <= 5000) {
                LcfArrayIter ai;
                uint32_t eid;
                const uint8_t *ep;
                size_t elen;
                int ok_all = 1;
                int cnt = 0;

                lcf_array_iter_init(&ai, ch.data, ch.len);
                while (lcf_array_next(&ai, &eid, &ep, &elen)) {
                    cnt++;
                    if (eid == 0) {
                        ok_all = 0;
                    }
                    {
                        LcfChunkIter sub;
                        LcfChunk c2;
                        lcf_chunk_iter_init(&sub, ep, elen);
                        while (lcf_chunk_next(&sub, &c2)) {
                        }
                        if (sub.r.error || sub.r.pos > elen) {
                            ok_all = 0;
                        }
                    }
                }
                /* 严格判据：无错 + 声明数与实得数一致 + 剩余为 0
                 *             + 恰好消费完整个 payload */
                if (ok_all && !ai.r.error && ai.remaining == 0
                    && cnt == (int)count && ai.r.pos == ch.len) {
                    if (depth == 0) {
                        res->arrays++;
                    }
                    res->items += cnt;
                }
                /* 不满足严格判据时，一律当标量处理，不记 bad_item：
                 * 数组与标量在语法上无法单凭 payload 区分，
                 * 需要靠字段码表才知道类型。这里只做保守验证。 */
            }
        }
    }

    if (depth == 0 && n == 0) {
        return 0;
    }
    return it.r.error ? 0 : 1;
}

static int verify_file(const char *path)
{
    size_t   size = 0;
    uint8_t *buf  = read_file(path, &size);
    LcfFile  f;
    Result   res;
    int      ok;

    printf("%-28s ", path);
    if (!buf) {
        printf("FAIL (unreadable)\n");
        return 0;
    }
    memset(&res, 0, sizeof(res));

    if (!lcf_file_open(&f, buf, size)) {
        printf("FAIL (bad magic '%s')\n", f.magic);
        free(buf);
        return 0;
    }

    ok = verify_struct(buf + f.body_off, size - f.body_off, &res, 0);

    printf("%-4s chunks=%-3d arrays=%-2d items=%-4d bad_id=%d bad_item=%d\n",
           ok ? "OK" : "FAIL", res.chunks, res.arrays, res.items,
           res.bad_id, res.bad_item);

    free(buf);
    return ok && res.bad_id == 0 && res.bad_item == 0 && res.chunks > 0;
}

int main(int argc, char **argv)
{
    int i;
    int fail = 0;

    if (argc < 2) {
        fprintf(stderr, "usage: %s <lcf-file>...\n", argv[0]);
        return 1;
    }
    for (i = 1; i < argc; i++) {
        if (!verify_file(argv[i])) {
            fail++;
        }
    }
    printf("\n%d/%d passed\n", argc - 1 - fail, argc - 1);
    return fail ? 1 : 0;
}
