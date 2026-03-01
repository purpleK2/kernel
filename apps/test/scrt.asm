format ELF64

section '.text' executable

extrn main

public _start
_start:
    mov rdi, rsp
    call main               ; main is expected to terminate via SYS_EXIT

; If main ever returns, spin as a last-resort fallback instead of exiting
; again based on whatever happens to be in RAX.
.hang:
    jmp .hang