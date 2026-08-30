; void _outb(uint16_t port, uint8_t val)
global _outb
_outb:
	mov dx, di
	mov ax, si

	; note for self
	; out PORT, VALUE
	out dx, al

	ret

; void _outw(uint16_t port, uint16_t val)
global _outw
_outw:
    mov dx, di
    mov ax, si

    out dx, ax

    ret

; void _outd(uint16_t port, uint32_t val)
global _outd
_outd:
    mov dx, di
    mov ax, si

    out dx, eax

    ret

; uint8_t _inb(uint16_t port)
global _inb
_inb:
	mov dx, di

	xor ax, ax

	; note for self
	; in OUT_REGISTER, PORT
	in al, dx

	ret

; uint16_t _inw(uint16_t port)
global _inw
_inw:
    mov dx, di
    xor ax, ax

    in ax, dx

    ret

; uint32_t _ind(uint16_t port)
global _ind
_ind:
    mov dx, di
    xor ax, ax

    in eax, dx

    ret
