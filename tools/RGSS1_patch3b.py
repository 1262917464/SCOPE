import io
src = io.open(r'E:\rgss2-rebuild\tools\unpacker3.c', encoding='utf-8').read()
anchor = 'eip=%08lx (target %08lx)'
i = src.find(anchor)
assert i >= 0
# 整段替换：把 eip 打印行扩展为带寄存器的打印（跨两行，手工定位行边界）
line_start = src.rfind('fprintf', 0, i)
line_end = src.find(';', i)
old = src[line_start:line_end + 1]
new = ('fprintf(stderr, "  eip=%08lx (target %08lx) EAX=%08lx EBX=%08lx ECX=%08lx EDX=%08lx\\n",\n'
       '                        (unsigned long)ctx.Eip, (unsigned long)(DLLBASE + oepRva),\n'
       '                        (unsigned long)ctx.Eax, (unsigned long)ctx.Ebx,\n'
       '                        (unsigned long)ctx.Ecx, (unsigned long)ctx.Edx);')
out = src[:line_start] + new + src[line_end + 1:]
io.open(r'E:\rgss2-rebuild\tools\RGSS1_unpacker3b.c', 'w', encoding='utf-8').write(out)
print('patched, old segment was:\n', old)
