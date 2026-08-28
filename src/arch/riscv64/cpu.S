.section ".text"

.global _hcf
_hcf:
    wfi
b halt_loop
halt_loop:
    b halt_loop
