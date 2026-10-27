/*
 * rgss_regex_stubs.c — 正则/编码 36 个导出的实现骨架
 *
 * 说明（M5-e 事实核对结果）:
 *   - 官方 GNU regex 符号 (ruby_re_*) : 由 Ruby 静态库提供, 需在 .def 里作为真实导出
 *   - Enterbrain 封装层 (regex_new/free/search/match/region_xxx/enc_get_xxx): 自研实现
 *   - 数据导出 (Reg 系列 / Enc 系列): 数据对象
 *
 * 本文件提供全部 36 个导出的可链接实现，其中:
 *   - 有明确语义的: 真实实现
 *   - 依赖原版内部结构的: 返回安全的空值, 标注 TODO
 */
#include "ruby.h"
#include <string.h>

/* Ruby 1.8.1 的 GNU regex 结构 (regex.h 内) */
#include "regex.h"

/* ================= 数据导出 ================= */
static const char s_enc_default[] = "ASCII-8BIT";
static const char s_enc_ascii[]   = "US-ASCII";
static const char s_enc_utf8[]    = "UTF-8";
static const char s_enc_eucjp[]   = "EUC-JP";
static const char s_enc_sjis[]    = "SJIS";
static void *s_syntax_default, *s_syntax_ruby;

void *EncDefaultCharEncoding(void) { return (void *)s_enc_default; }
void *RegDefaultSyntax(void)       { return s_syntax_default; }
void *RegSyntaxRuby(void)          { return s_syntax_ruby; }
void *RegEncodingASCII(void)       { return (void *)s_enc_ascii; }
void *RegEncodingUTF8(void)        { return (void *)s_enc_utf8; }
void *RegEncodingEUC_JP(void)      { return (void *)s_enc_eucjp; }
void *RegEncodingSJIS(void)        { return (void *)s_enc_sjis; }

void rgss_regex_init_globals(void)
{
    extern struct re_syntax *rb_syntax_options(void);
    s_syntax_default = NULL;
    s_syntax_ruby    = (void *)rb_intern("ruby");   /* 占位, M5-e 精修 */
}

/* ================= 官方 GNU regex ================= */
/* ruby_re_* 由 Ruby 1.8.1 静态库 (regex.c) 直接提供, 签名见 regex.h.
   本单元不重复声明; .def 里以真实符号导出即可。 */

/* ================= Enterbrain 封装层 ================= */
/* regex_new: 编译正则 (原版 0x1007C2F0)
   签名推断自反编译: (handle**, pattern, len, option, syntax, err_out) -> errcode */
typedef struct EbRegex {
    void *pattern;          /* 原版 0x1007C230 分配的内部句柄 */
    int   options;
    void *syntax;
} EbRegex;

int regex_new(void *handle_out, const char *pattern, int len, int option,
              void *syntax, int *err_out)
{
    if (err_out) *err_out = 0;
    /* TODO(M5-e): 按原版 0x1007C230/0x1007BF30 语义实现
       目前返回编译错误码 0 = 成功但未真正编译 */
    (void)handle_out; (void)pattern; (void)len; (void)option; (void)syntax;
    return 0;
}

void regex_free(void *handle) { (void)handle; }
int  regex_search(void *handle, const char *str, int len, int start, int range,
                  void *region, int options)
{ (void)handle; (void)str; (void)len; (void)start; (void)range; (void)region; (void)options; return -1; }
int  regex_match(void *handle, const char *str, int len, int start, void *region)
{ (void)handle; (void)str; (void)len; (void)start; (void)region; return -1; }
int  regex_init(void) { return 0; }
int  regex_end(void) { return 0; }
const char *regex_version(void) { return "0.12-gnu"; }
void *regex_region_new(void) { return NULL; }
void  regex_region_free(void *r) { (void)r; }
int   regex_region_clear(void *r) { (void)r; return 0; }
int   regex_region_copy(void *d, void *s) { (void)d; (void)s; return 0; }
int   regex_region_resize(void *r, int n) { (void)r; (void)n; return 0; }
int   regex_error_code_to_str(int code) { (void)code; return 0; }
int   regex_foreach_name(void *h, void *cb, void *arg) { (void)h; (void)cb; (void)arg; return 0; }
int   regex_name_to_backref_number(void *h, const char *n, int len) { (void)h; (void)n; (void)len; return -1; }
int   regex_name_to_group_numbers(void *h, const char *n, int len, int **out) { (void)h; (void)n; (void)len; (void)out; return -1; }
int   regex_number_of_names(void *h) { (void)h; return 0; }
int   regex_get_encoding(void *h) { (void)h; return 0; }
int   regex_get_options(void *h) { (void)h; return 0; }
void *regex_get_syntax(void *h) { (void)h; return NULL; }
int   regex_recompile(void *h) { (void)h; return 0; }
void  regex_set_verb_warn_func(void *f) { (void)f; }
void  regex_set_warn_func(void *f) { (void)f; }
void *re_alloc_pattern(void) { return NULL; }

/* enc_get_* : 字符边界调整 (原版基于编码表) */
int enc_get_left_adjust_char_head(void *enc, const char *start, const char *p)
{ (void)enc; return (int)(p - start); }
int enc_get_prev_char_head(void *enc, const char *start, const char *p)
{ (void)enc; return (p > start) ? (int)(p - start - 1) : -1; }
int enc_get_right_adjust_char_head(void *enc, const char *start, const char *p)
{ (void)enc; return (int)(p - start); }

/* ================= 1.8.1 缺失符号的兼容实现 ================= */
/* ruby_re_recompile_pattern 在 1.8.1 官方 regex.c 中不存在,
   原版 RGSS 导出的是 Enterbrain 自行添加的封装。
   语义: 用新选项重新编译已存在的 pattern。 */
int ruby_re_recompile_pattern(void *pat, const char *pattern, int len,
                              int option, void *syntax, int *err)
{
    (void)pat; (void)pattern; (void)len; (void)option; (void)syntax;
    if (err) *err = 0;
    return 0;
}
