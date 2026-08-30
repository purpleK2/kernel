add_languages('nasm')

sources += [
  'arch/x86_64/cpu.asm'
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
