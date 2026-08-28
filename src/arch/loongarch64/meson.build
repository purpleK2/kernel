arch_sources = [
  'arch/loongarch64/cpu.S'
]

sources += arch_sources

if (not cc.has_argument('-march=loongarch64'))
  error('C Compiler does not support loongarch64 (loongarch64) target!')
endif

c_args += [
  '-march=loongarch64',
  '-mabi=lp64s',
  '-mfpu=none',
  '-msimd=none',
  '-mno-relax'
]

linker_flags += [
  '-Wl,-melf64loongarch',
  '-Wl,--no-relax'
]
