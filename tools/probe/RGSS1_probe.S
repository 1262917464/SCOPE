# RGSS1_probe.S — RGSS1(103J) 四槽探针(GAS/i386): 记录返回地址与参数后原样返回
    .text
    .irp    P, a, b, c, d
    .globl  _probe_\P
_probe_\P:
    pushal
    leal    36(%esp), %eax
    pushl   %eax
    pushl   40(%esp)
    pushl   $_h\P
    call    _log_call
    addl    $12, %esp
    popal
    ret
    .endr

    .section .rdata
_ha: .asciz "SLOT_A(0x0367E0)"
_hb: .asciz "SLOT_B(0x0E730C)"
_hc: .asciz "SLOT_C(0x0E73A4)"
_hd: .asciz "SLOT_D(0x0E73C8)"
