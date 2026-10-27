# RGSS1_fixstub.S — 语义修复桩（stdcall，按调用点观测的参数个数弹栈）
#   0xE73A4(1参) -> 返回 1（可见性类）
#   0xE730C(3参) / 0xE73C8(5参) -> 返回真 GetTickCount()
#   0x367E0(未知) -> 原样返回（不改动）
    .text
    .globl _fix_visible
_fix_visible:
    movl    $1, %eax
    ret     $4

    .globl _fix_tick3
_fix_tick3:
    call    _get_tick
    ret     $12

    .globl _fix_tick5
_fix_tick5:
    call    _get_tick
    ret     $20

    .globl _fix_unknown
_fix_unknown:
    ret
