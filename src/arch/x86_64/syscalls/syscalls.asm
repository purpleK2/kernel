format ELF64

section '.text' executable align 16

extrn syscall_table ; void* syscall_table[]
extrn change_to_kernel_pml4_on_int
extrn get_kernel_pml4
extrn set_syscall_context

; void* index_to_ptr(size_t index)
public index_to_ptr
index_to_ptr:
    mov rax, rdi
    shl rax, 3
    add rax, syscall_table
    mov rax, [rax]
    ret

; void syscall_handler() <-- gets directly inserted into the IDT for performance
public syscall_handler
syscall_handler:
    push 0
    push 0x80
    
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

    mov rax, cr3
    push rax
    
    mov bx, 0x10
    mov ds, bx
    mov es, bx

    cmp dword [change_to_kernel_pml4_on_int], 0
    je .no_switch
    push rax
    call get_kernel_pml4
    mov cr3, rax
    pop rax
.no_switch:
    mov rbp, rsp
    
    mov rdi, rbp
    call set_syscall_context
    
    mov rdi, qword [rbp + 0x80]
    call index_to_ptr
    mov r12, rax

    mov rdi, qword [rbp + 0x58]
    mov rsi, qword [rbp + 0x60]
    mov rdx, qword [rbp + 0x68]
    mov rcx, qword [rbp + 0x48]
    mov r8,  qword [rbp + 0x40]
    mov r9,  qword [rbp + 0x38]
    call r12

    mov qword [rbp + 0x80], rax

    pop rax
    mov cr3, rax

    pop rbp
    mov bx, bp
    mov ds, bx
    mov es, bx
    
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