extern isr_handler

isr_common:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov rbp, ds
    push rbp

    mov rdi, rsp
    call isr_handler

    pop rbp
    mov ds, ebp
    mov es, ebp
    mov fs, ebp
    mov gs, ebp
    mov ss, ebp
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    add rsp, 16
    iretq

; create stubs for each exception (8, 10-14, 17 and 21 give error codes)
%assign i 0
%rep    256
global isr_stub_%+i
isr_stub_%+i:
    %if i <> 8 && i <> 10 && i <> 11 && i <> 12 && i <> 13 && i <> 14 && i <> 17 && i <> 21
    push 0              ; dummy error code for exceptions without error codes
    %endif
    push %+i             ; interrupt number
    jmp isr_common
%assign i i+1
%endrep

; void** isr_stub_table
global isr_stub_table
isr_stub_table:
    %assign i 0
    %rep 256
    dq isr_stub_%+i
    %assign i i+1
    %endrep
