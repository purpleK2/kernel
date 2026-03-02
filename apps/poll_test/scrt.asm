format ELF64

section '.text' executable

extrn main

public _start
_start:
    mov rdi, rsp
    call main

.hang:
    jmp .hang
