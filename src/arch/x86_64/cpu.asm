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
    cmp rsi, 0
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

global _get_cr3
_get_cr3:
    mov rax, cr3
    ret

; uint64_t _rdmsr(uint64_t m)
global _rdmsr
_rdmsr:
    ; rdmsr takes the MSR from ECX
    mov ecx, edi

    xor rax, rax
    xor rdx, rdx
    rdmsr
    ; rax |= (rdx << 32)
    shl rdx, 32
    or rax, rdx

    ret

; void _wrmsr(uint64_t m, uint64_t v)
global _wrmsr
_wrmsr:
    mov ecx, edi

    xor rax, rax
    xor rdx, rdx
    ; v & 32
    mov rax, rsi
    ; v >> 32
    mov rdx, rax
    shr rdx, 32

    wrmsr
    ret
