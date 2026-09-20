; void _invlpg(uint64_t pg)
global _invlpg
_invlpg:
	invlpg [rdi]
	ret

global pg_switch
pg_switch:
    mov cr3, rdi
    ret
