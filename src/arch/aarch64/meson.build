sources += [
  'arch/aarch64/cpu.S'
]

if (not cc.has_argument('-march=armv8-a+nofp+nosimd'))
  error('C Compiler does not support aarch64 (armv8-a+nofp+nosimd) target!')
endif

c_args += [
  '-march=armv8-a+nofp+nosimd',
  '-mtune=generic',
  '-mgeneral-regs-only',
  '-mno-outline-atomics'
]

linker_flags += [
  '-Wl,-maarch64elf'
]
