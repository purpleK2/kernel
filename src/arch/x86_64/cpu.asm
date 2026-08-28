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
