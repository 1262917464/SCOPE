/*
 * lcf_test.c —— LCF 解析器验证程序
 *
 * 验证方式：把每个测试文件的顶层与二级结构完整解出并统计，
 * 与文件自身特征（size/层数据长度/数组条目数）交叉核对。
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

/* 判断一个 payload 是否看起来像数组（首 BER 是合理 count） */
static int looks_like_array(const uint8_t *data, size_t len)
{
    LcfReader r;
    uint32_t  count;
    uint32_t  id;

    if (len < 2) {
        return 0;
    }
    lcf_reader_init(&r, data, len);
    count = lcf_read_ber(&r);
    if (r.error || count == 0 || count > 5000) {
        return 0;
    }
    id = lcf_read_ber(&r);
    if (r.error || id == 0 || id > 0x10000) {
        return 0;
    }
    return 1;
}

static void dump_structure(const char *path)
{
    size_t   size = 0;
    uint8_t *buf  = read_file(path, &size);
    LcfFile  f;
    LcfChunkIter it;
    LcfChunk ch;
    int      n = 0, arrays = 0;

    if (!buf) {
        printf("  !! cannot read %s\n", path);
        return;
    }

    printf("\n%s  (%zu bytes)\n", path, size);

    if (!lcf_file_open(&f, buf, size)) {
        printf("  !! bad header (magic=%s)\n", f.magic);
        free(buf);
        return;
    }
    printf("  kind=%s magic=%s body_off=%zu body=%zu\n",
           lcf_kind_name(f.kind), f.magic, f.body_off, size - f.body_off);

    lcf_chunk_iter_init(&it, buf + f.body_off, size - f.body_off);
    while (lcf_chunk_next(&it, &ch)) {
        int isarr = looks_like_array(ch.data, ch.len);
        if (isarr) {
            arrays++;
        }
        if (n < 32) {
            printf("    [%2d] id=%#04x len=%6zu%s", n, ch.id, ch.len,
                   isarr ? "  <array>" : "");
            if (isarr) {
                LcfArrayIter ai;
                uint32_t eid;
                const uint8_t *ep;
                size_t elen;
                int cnt = 0;
                lcf_array_iter_init(&ai, ch.data, ch.len);
                while (lcf_array_next(&ai, &eid, &ep, &elen)) {
                    if (cnt < 3) {
                        printf(" item[%u]={%zuB}", eid, elen);
                    }
                    cnt++;
                }
                printf(" count=%d", cnt);
            }
            printf("\n");
        }
        n++;
    }

    printf("  => 顶层 %d 个 chunk，其中 %d 个数组；iter%s\n",
           n, arrays, it.r.error ? " 报错" : " 正常结束");
    free(buf);
}

int main(int argc, char **argv)
{
    int i;
    if (argc < 2) {
        fprintf(stderr, "usage: %s <lcf-file>...\n", argv[0]);
        return 1;
    }
    for (i = 1; i < argc; i++) {
        dump_structure(argv[i]);
    }
    return 0;
}
