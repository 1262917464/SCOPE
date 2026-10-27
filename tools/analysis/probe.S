# probe.S — naked 探针(GAS/i386): 记录返回地址与参数后原样返回
    .text
    .globl _probe_a
_probe_a:
    pushal                  # esp=P, [P+32]=retaddr, [P+36]=arg1
    leal    36(%esp), %eax  # &arg1
    pushl   %eax            # arg3: args
    pushl   40(%esp)        # arg2: retaddr
    pushl   $_hA            # arg1: tag
    call    _log_call
    addl    $12, %esp
    popal
    ret                     # 不弹参数(原 API 可能是 stdcall, 弹栈交给调用方语义观察)

    .globl _probe_b
_probe_b:
    pushal
    leal    36(%esp), %eax
    pushl   %eax
    pushl   40(%esp)
    pushl   $_hB
    call    _log_call
    addl    $12, %esp
    popal
    ret

    .section .rdata
_hA: .asciz "SLOT_A(0xFF2DC)"
_hB: .asciz "SLOT_B(0xFF324)"
