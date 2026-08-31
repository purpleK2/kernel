add_languages('nasm')

sources += [
  'arch/x86_64/cpu.asm',
  'arch/x86_64/io.asm',
  'arch/x86_64/debug.c',
  'arch/x86_64/arch.c',
  'arch/x86_64/gdt/gdt.c',
  'arch/x86_64/gdt/gdt.asm',
  'arch/x86_64/interrupts/idt.c',
  'arch/x86_64/interrupts/isr.c',
  'arch/x86_64/interrupts/isr.asm',
  'arch/x86_64/interrupts/idt.asm'
]

if (not cc.has_argument('-march=x86-64'))
  error('C Compiler does not support x86_64 (x86-64) target!')
endif

nasm_args = [
  '-F', 'dwarf',
  '-Wall',
  '-f', 'elf64'
]

c_args += [
  '-m64',
  '-march=x86-64',
  '-mabi=sysv',
  '-mno-80387',
  '-mno-mmx',
  '-mno-sse',
  '-mno-sse2',
  '-mno-red-zone',
  '-mcmodel=kernel'
]

linker_flags += [
  '-Wl,-melf_x86_64'
]
