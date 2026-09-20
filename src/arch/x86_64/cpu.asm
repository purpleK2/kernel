global _hcf
_hcf:
    cli
.hcf_loop:
    hlt
    jmp .hcf_loop

global _disable_interrupts
_disable_interrupts:
    cli
    ret

global _enable_interrupts
_enable_interrupts:
    sti
    ret

; int _cpuid(uint32_t leaf, struct cpuid_ctx* ctx)
global _cpuid
_cpuid:
    cmp rdi, 0
    je .nullptr

    push rbx
    push rcx
    push rdx
    xor rax, rax
    mov rax, rdi

    cpuid

    mov [rsi], eax
    mov [rsi + 4], ebx
    mov [rsi + 8], ecx
    mov [rsi + 12], edx

    pop rdx
    pop rcx
    pop rbx

    mov rax, 0
    ret
.nullptr:
    mov rax, -1
    ret
