/*
 * rgss_embedded.c — 内嵌 RPG 类源码注入
 * 源码由 objcopy 打包为 build/rpg_src.o（来自 docs/assets/embedded_rpg_classes.rb）
 * 符号: _binary_build_rpg_rb_start/_end（COFF 已含前导下划线），用 asm 标签精确绑定。
 */
#include <windows.h>
#include "ruby.h"

extern const unsigned char rpg_src_start[] asm("_binary_build_rpg_rb_start");
extern const unsigned char rpg_src_end[]   asm("_binary_build_rpg_rb_end");

int rgss_eval_embedded_rpg(void)
{
    size_t len = (size_t)(rpg_src_end - rpg_src_start);
    if (len == 0 || len > 1024 * 1024) return 0;
    char *buf = (char *)malloc(len + 1);
    if (!buf) return 0;
    memcpy(buf, rpg_src_start, len);
    buf[len] = 0;
    int st = 0;
    rb_eval_string_protect(buf, &st);
    free(buf);
    return st == 0;
}
