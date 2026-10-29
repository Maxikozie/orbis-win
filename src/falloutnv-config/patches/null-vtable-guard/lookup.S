.section .text
.code32
.global lookup
lookup:
    push %ebx
    push %esi
    push %edi
    mov 0x10(%esp), %ebx
    cmp $7, %ebx
    jl .Lzero
    mov 0x24(%ecx), %esi
.Lloop:
    test %esi, %esi
    jz .Lzero
    mov 8(%esi), %edi
    mov (%esi), %esi
    test %edi, %edi
    jz .Lloop
    mov (%edi), %eax
    test %eax, %eax
    jz .Lloop
    mov 0x8c(%eax), %edx
    mov %edi, %ecx
    call *%edx
    cmp %ebx, %eax
    jne .Lloop
    mov %edi, %eax
    jmp .Lexit
.Lzero:
    xor %eax, %eax
.Lexit:
    pop %edi
    pop %esi
    pop %ebx
    ret $4
