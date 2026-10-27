/*
 * lcf.h —— LCF 二进制格式（RM2K / RM2K3 数据格式）
 *
 * 格式来源（双证据）：
 *   A. 原版 exe 逆向
 *        0x45EBCC  ReadBerLen       变长整数
 *        0x45EC00  ReadHeaderStr    BER 长度 + 字节
 *        0x469834  TLcfDataBase.Load  顶层 + 15 路版本分派
 *        0x469A44  TLcfDataBase.LoadFromFile  文件/缓存层
 *   B. liblcf 源码交叉验证（仅作参照，实现为本项目独立 C 代码）
 *        src/reader_lcf.cpp    ReadInt / Read<double> / byte-swap
 *        src/reader_struct_impl.h  ReadLcf 的 chunk 循环
 *
 * === BER 变长整数 ===
 * 原版 0x45EBCC 与 liblcf ReadInt 完全一致：
 *     value = 0
 *     do {
 *         value <<= 7                       ; 先移位
 *         value |= (byte & 0x7F)            ; 再加 7 位
 *     } while (byte & 0x80)
 *
 * 即"高位在前"的 7 位分组，不是标准 LEB128。两次独立来源一致。
 *
 * === 文件头 ===
 *     BER   magic_len
 *     byte  magic[magic_len]                ; "LcfDataBase"/"LcfMapUnit"/"LcfMapTree"/"LcfSaveData"
 *     --- 之后直接是 chunk 流，没有总长度字段 ---
 *
 * === 块（Chunk） ===
 *     BER   ID                              ; 字段码
 *     BER   length                          ; 载荷字节数
 *     byte  data[length]
 *
 * ★ ID 在前、length 在后 —— 顺序反了会解出一堆不可能的字段码。
 *   ID == 0 表示结构结束（ReadLcf 里 `if (chunk_info.ID == 0) break;`）。
 *
 * === 数组 ===
 *     BER   count
 *     count × {
 *         BER   ID                          ; 元素编号（1 基）
 *         ...   Struct（chunk 流，以 ID=0 结束）
 *     }
 *
 * 数组不是"一个 chunk 里套 N 个 chunk"，而是"count + N×(ID + struct)"。
 */
#ifndef LCF_H
#define LCF_H

#include <stdint.h>
#include <stddef.h>

/* ------------------------------------------------------------------ */
/* 读取器                                                              */
/* ------------------------------------------------------------------ */

typedef struct LcfReader {
    const uint8_t *buf;
    size_t         size;
    size_t         pos;
    int            error;      /* 非 0 表示越界/格式错误 */
} LcfReader;

void     lcf_reader_init(LcfReader *r, const uint8_t *buf, size_t size);
int      lcf_reader_eof(const LcfReader *r);
size_t   lcf_reader_tell(const LcfReader *r);

int      lcf_read_byte(LcfReader *r);
uint32_t lcf_read_ber(LcfReader *r);          /* 对应 0x45EBCC */
int32_t  lcf_read_int(LcfReader *r);          /* = BER，有符号容器 */
int32_t  lcf_read_i32(LcfReader *r);          /* 定长 4 字节小端（稀有） */
uint16_t lcf_read_u16(LcfReader *r);
int8_t   lcf_read_i8(LcfReader *r);
double   lcf_read_double(LcfReader *r);       /* 8 字节小端 IEEE754 */
int      lcf_read_bool(LcfReader *r);         /* BER != 0 */

const uint8_t *lcf_read_bytes(LcfReader *r, size_t n);
void     lcf_reader_skip(LcfReader *r, size_t n);

/* 读 "BER 长度 + 原始字节"，返回 malloc 的 NUL 结尾串（可能为 NULL） */
char    *lcf_read_string(LcfReader *r);
void     lcf_free_string(char *s);

/* ------------------------------------------------------------------ */
/* 块结构                                                              */
/* ------------------------------------------------------------------ */

typedef struct LcfChunk {
    uint32_t       id;         /* 字段码 */
    const uint8_t *data;
    size_t         len;
} LcfChunk;

/*
 * 在一个 struct 的 chunk 流上迭代。
 * 遇到 ID == 0 停止（该结构结束）。
 * `limit` 是该结构的字节上界（来自外层 chunk 的 length，或整个正文）。
 */
typedef struct LcfChunkIter {
    LcfReader r;
    size_t    limit;
    int       done;
} LcfChunkIter;

void lcf_chunk_iter_init(LcfChunkIter *it, const uint8_t *buf, size_t len);
int  lcf_chunk_next(LcfChunkIter *it, LcfChunk *out);

/* 数组迭代：先读 count，再逐个 (ID + struct) */
typedef struct LcfArrayIter {
    LcfReader r;
    int32_t   remaining;
} LcfArrayIter;

void    lcf_array_iter_init(LcfArrayIter *it, const uint8_t *buf, size_t len);
/* 返回 1 表示拿到一个元素；元素边界写入 *elem_id / *elem（struct chunk 流） */
int     lcf_array_next(LcfArrayIter *it, uint32_t *elem_id,
                       const uint8_t **elem, size_t *elem_len);

/* ------------------------------------------------------------------ */
/* 顶层文件                                                            */
/* ------------------------------------------------------------------ */

typedef enum LcfFileKind {
    LCF_KIND_UNKNOWN = 0,
    LCF_KIND_LDB,          /* LcfDataBase */
    LCF_KIND_LMT,          /* LcfMapTree  */
    LCF_KIND_LMU,          /* LcfMapUnit  */
    LCF_KIND_LSD           /* LcfSaveData */
} LcfFileKind;

typedef struct LcfFile {
    const uint8_t *buf;
    size_t         size;
    LcfFileKind    kind;
    char           magic[32];
    size_t         body_off;   /* 正文起始（magic 之后） */
} LcfFile;

int  lcf_file_open(LcfFile *f, const uint8_t *buf, size_t size);
const char *lcf_kind_name(LcfFileKind k);

/* ------------------------------------------------------------------ */
/* 字段码（LDB 顶级容器）                                              */
/* ------------------------------------------------------------------ */

enum {
    LDB_ACTORS      = 0x0B,
    LDB_SKILLS      = 0x0C,
    LDB_ITEMS       = 0x0D,
    LDB_ENEMIES     = 0x0E,
    LDB_TROOPS      = 0x0F,
    LDB_TERRAIN     = 0x10,
    LDB_ATTRIBS     = 0x11,
    LDB_CONDITIONS  = 0x12,
    LDB_BATTLECOMM  = 0x13,
    LDB_ANIMATIONS  = 0x14,
    LDB_CHIPSETS    = 0x15,
    LDB_TERMS       = 0x16,
    LDB_SYSTEM      = 0x17,
    LDB_SWITCHES    = 0x18,
    LDB_VARIABLES   = 0x19,
    LDB_COMMONEVTS  = 0x1A,
    LDB_BATTLE      = 0x1C,
    LDB_CLASSES     = 0x1D,
    LDB_BATTLER     = 0x1E,
    LDB_CBAMONSTER  = 0x1F,
    LDB_CBASKILL    = 0x20,
    LDB_CBAITEM     = 0x21,
    LDB_CBACOND     = 0x22,
    LDB_CBAATTR     = 0x23,
};

/* LMU 顶级块码 */
enum {
    LMU_SAVECOUNT   = 0x01,
    LMU_CHIPOFFSET  = 0x02,
    LMU_WIDTH       = 0x03,
    LMU_HEIGHT      = 0x04,
    LMU_LOWER       = 0x05,
    LMU_UPPER       = 0x06,
    LMU_EVENTS      = 0x51,
};

#endif /* LCF_H */
