/*
 * lcf.c —— LCF 读取原语
 *
 * 每个函数标注对应的原版地址与 liblcf 参照点，便于逐项核对。
 */
#include "lcf.h"

#include <stdlib.h>
#include <string.h>

void lcf_reader_init(LcfReader *r, const uint8_t *buf, size_t size)
{
    r->buf   = buf;
    r->size  = size;
    r->pos   = 0;
    r->error = 0;
}

int lcf_reader_eof(const LcfReader *r)
{
    return r->pos >= r->size;
}

size_t lcf_reader_tell(const LcfReader *r)
{
    return r->pos;
}

static int need(LcfReader *r, size_t n)
{
    if (r->pos + n > r->size) {
        r->error = 1;
        return 0;
    }
    return 1;
}

int lcf_read_byte(LcfReader *r)
{
    if (!need(r, 1)) {
        return 0;
    }
    return r->buf[r->pos++];
}

/*
 * 对应原版 0x45EBCC，与 liblcf LcfReader::ReadInt 一致：
 *   value <<= 7
 *   value |= (b & 0x7F)
 *   while (b & 0x80)
 */
uint32_t lcf_read_ber(LcfReader *r)
{
    uint32_t value = 0;
    int      b;
    int      loops = 0;

    do {
        b = lcf_read_byte(r);
        if (r->error) {
            return 0;
        }
        value = (value << 7) | (uint32_t)(b & 0x7F);
        if (++loops > 5) {
            r->error = 1;   /* liblcf 同样在 >5 次后判非法 */
            return 0;
        }
    } while (b & 0x80);

    return value;
}

int32_t lcf_read_int(LcfReader *r)
{
    return (int32_t)lcf_read_ber(r);
}

int32_t lcf_read_i32(LcfReader *r)
{
    int32_t v;
    if (!need(r, 4)) {
        return 0;
    }
    memcpy(&v, r->buf + r->pos, 4);
    r->pos += 4;
    return v;
}

uint16_t lcf_read_u16(LcfReader *r)
{
    uint16_t v;
    if (!need(r, 2)) {
        return 0;
    }
    memcpy(&v, r->buf + r->pos, 2);
    r->pos += 2;
    return v;
}

int8_t lcf_read_i8(LcfReader *r)
{
    return (int8_t)lcf_read_byte(r);
}

double lcf_read_double(LcfReader *r)
{
    double v;
    if (!need(r, 8)) {
        return 0.0;
    }
    memcpy(&v, r->buf + r->pos, 8);
    r->pos += 8;
    return v;
}

int lcf_read_bool(LcfReader *r)
{
    return lcf_read_ber(r) != 0;
}

const uint8_t *lcf_read_bytes(LcfReader *r, size_t n)
{
    const uint8_t *p;
    if (!need(r, n)) {
        return NULL;
    }
    p = r->buf + r->pos;
    r->pos += n;
    return p;
}

void lcf_reader_skip(LcfReader *r, size_t n)
{
    if (!need(r, n)) {
        return;
    }
    r->pos += n;
}

/*
 * 对应原版 0x45EC00：BER 长度 + 原始字节。
 * liblcf: reader.ReadString(header, reader.ReadInt())
 */
char *lcf_read_string(LcfReader *r)
{
    uint32_t n = lcf_read_ber(r);
    const uint8_t *p;
    char *s;

    if (r->error) {
        return NULL;
    }
    if (n == 0) {
        return NULL;
    }
    p = lcf_read_bytes(r, n);
    if (!p) {
        return NULL;
    }
    s = (char *)malloc((size_t)n + 1);
    if (!s) {
        return NULL;
    }
    memcpy(s, p, n);
    s[n] = '\0';
    return s;
}

void lcf_free_string(char *s)
{
    free(s);
}

/* ------------------------------------------------------------------ */
/* chunk 迭代                                                          */
/* ------------------------------------------------------------------ */

void lcf_chunk_iter_init(LcfChunkIter *it, const uint8_t *buf, size_t len)
{
    lcf_reader_init(&it->r, buf, len);
    it->limit = len;
    it->done  = 0;
}

/*
 * 对应 liblcf Struct<S>::ReadLcf 的循环：
 *     ID = ReadInt();  if (ID == 0) break;
 *     length = ReadInt();
 *     ...
 *
 * ★ ID 在前、length 在后。
 */
int lcf_chunk_next(LcfChunkIter *it, LcfChunk *out)
{
    uint32_t id;
    uint32_t len;

    if (it->done || it->r.error) {
        return 0;
    }
    if (it->r.pos >= it->limit) {
        it->done = 1;
        return 0;
    }

    id = lcf_read_ber(&it->r);
    if (it->r.error) {
        it->done = 1;
        return 0;
    }
    if (id == 0) {
        it->done = 1;
        return 0;
    }

    len = lcf_read_ber(&it->r);
    if (it->r.error) {
        it->done = 1;
        return 0;
    }

    out->id   = id;
    out->data = lcf_read_bytes(&it->r, len);
    out->len  = len;

    if (!out->data) {
        it->done = 1;
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* 数组迭代                                                            */
/* ------------------------------------------------------------------ */

void lcf_array_iter_init(LcfArrayIter *it, const uint8_t *buf, size_t len)
{
    lcf_reader_init(&it->r, buf, len);
    it->remaining = (int32_t)lcf_read_ber(&it->r);
    if (it->remaining < 0 || it->remaining > 1000000) {
        it->remaining = 0;
        it->r.error   = 1;
    }
}

/*
 * 元素 = BER ID + struct（struct 是 chunk 流，以 ID=0 结束）。
 *
 * 注意：结构体没有显式长度前缀，所以必须靠 chunk 流自身的
 * ID==0 终止符来定界。这里手工走一遍以求出元素边界。
 */
int lcf_array_next(LcfArrayIter *it, uint32_t *elem_id,
                   const uint8_t **elem, size_t *elem_len)
{
    LcfReader sub;
    size_t    start;
    uint32_t  id;

    if (it->remaining <= 0 || it->r.error) {
        return 0;
    }

    id = lcf_read_ber(&it->r);
    if (it->r.error) {
        it->remaining = 0;
        return 0;
    }
    *elem_id = id;

    /* 从当前位置开始探测 chunk 流，直到遇到 ID==0 */
    start = it->r.pos;
    sub   = it->r;
    for (;;) {
        uint32_t cid, clen;
        cid = lcf_read_ber(&sub);
        if (sub.error) {
            it->remaining = 0;
            it->r.error   = 1;
            return 0;
        }
        if (cid == 0) {
            break;
        }
        clen = lcf_read_ber(&sub);
        if (sub.error) {
            it->remaining = 0;
            it->r.error   = 1;
            return 0;
        }
        lcf_reader_skip(&sub, clen);
        if (sub.error) {
            it->remaining = 0;
            it->r.error   = 1;
            return 0;
        }
    }

    *elem     = it->r.buf + start;
    *elem_len = sub.pos - start;

    /* 消费掉结构体 + 终止符 */
    it->r.pos = sub.pos;
    it->remaining--;
    return 1;
}

/* ------------------------------------------------------------------ */
/* 顶层文件                                                            */
/* ------------------------------------------------------------------ */

int lcf_file_open(LcfFile *f, const uint8_t *buf, size_t size)
{
    LcfReader r;
    char *magic;

    memset(f, 0, sizeof(*f));
    f->buf  = buf;
    f->size = size;

    lcf_reader_init(&r, buf, size);
    magic = lcf_read_string(&r);
    if (!magic) {
        return 0;
    }
    strncpy(f->magic, magic, sizeof(f->magic) - 1);
    f->body_off = r.pos;
    lcf_free_string(magic);

    if (strcmp(f->magic, "LcfDataBase") == 0) {
        f->kind = LCF_KIND_LDB;
    } else if (strcmp(f->magic, "LcfMapTree") == 0) {
        f->kind = LCF_KIND_LMT;
    } else if (strcmp(f->magic, "LcfMapUnit") == 0) {
        f->kind = LCF_KIND_LMU;
    } else if (strcmp(f->magic, "LcfSaveData") == 0) {
        f->kind = LCF_KIND_LSD;
    } else {
        f->kind = LCF_KIND_UNKNOWN;
        return 0;
    }
    return 1;
}

const char *lcf_kind_name(LcfFileKind k)
{
    switch (k) {
    case LCF_KIND_LDB: return "LDB";
    case LCF_KIND_LMT: return "LMT";
    case LCF_KIND_LMU: return "LMU";
    case LCF_KIND_LSD: return "LSD";
    default:           return "?";
    }
}
