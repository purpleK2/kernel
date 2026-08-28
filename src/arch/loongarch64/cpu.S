.section ".text"

.global _hcf
_hcf:
    idle 0
b halt_loop
halt_loop:
    b halt_loop
