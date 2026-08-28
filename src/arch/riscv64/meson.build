arch_sources = [
  'arch/riscv64/cpu.S'
]

sources += arch_sources

if (not cc.has_argument('-march=rv64imac_zicsr_zifencei'))
  error('C Compiler does not support riscv64 (rv64imac_zicsr_zifencei) target!')
endif

c_args += [
  '-march=rv64imac_zicsr_zifencei',
  '-mabi=lp64',
  '-mno-relax'
]

linker_flags += [
  '-Wl,-melf64lriscv',
  '-Wl,--no-relax'
]
