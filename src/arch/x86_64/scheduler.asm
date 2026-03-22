format ELF64
section '.text' executable align 16

public context_load
context_load:
    sti
    mov r10, [rdi + 0x98]
    mov r11, [rdi + 0xA0]
    mov r12, [rdi + 0xA8]
    mov r13, [rdi + 0xB0]
    mov r14, [rdi + 0xB8]

    mov r15, [rdi + 0x10]

    push r14                ; ss
    push r13                ; rsp
    push r12                ; rflags
    push r11                ; cs
    push r10                ; rip

    mov r14, [rdi + 0x18]
    mov r13, [rdi + 0x20]
    mov r12, [rdi + 0x28]
    mov r11, [rdi + 0x30]
    mov r10, [rdi + 0x38]

    mov r9,  [rdi + 0x40]
    mov r8,  [rdi + 0x48]
    mov rbp, [rdi + 0x50]
    mov rsi, [rdi + 0x60]
    mov rdx, [rdi + 0x68]
    mov rcx, [rdi + 0x70]
    mov rbx, [rdi + 0x78]
    mov rax, [rdi + 0x80]

    mov ax, [rdi + 0x08]
    mov ds, ax
    mov es, ax
    mov rax, [rdi + 0x80]

    mov rdi, [rdi + 0x58]

    iretq

public fpu_save
fpu_save:
    fxsave [rdi]
    ret

public fpu_restore
fpu_restore:
    fxrstor [rdi]
    ret

public scheduler_idle
scheduler_idle:
    sti
    hlt
    jmp scheduler_idle