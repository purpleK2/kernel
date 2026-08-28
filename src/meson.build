c_args = [
  '-Wall',
  '-Wextra',
  '-std=gnu11',
  '-nostdinc',
  '-ffreestanding',
  '-fno-stack-protector',
  '-fno-stack-check',
  '-fno-lto',
  '-fno-PIC',
  '-ffunction-sections',
  '-fdata-sections',
  '-MMD',
  '-MP'
]

sources = [
  'kernel/kernel.c'
]

linker_script = join_paths(meson.current_source_dir(), 'arch'/target_arch/'linker.ld')

linker_flags = [
  '-nostdlib',
  '-static',
  '-Wl,-z,max-page-size=0x1000',
  '-Wl,--gc-sections',
  '-Wl,-T,' + linker_script
]

# adds all platform-specific flags and sources.
subdir('arch'/target_arch)

if (target_arch == 'x86_64' )
  executable('kernel',
            sources,
            dependencies: [limine_proto, freestanding_hdrs],
            include_directories: archcommon_inc,
            c_args: c_args,
            nasm_args: nasm_args,
            link_args: linker_flags,
            link_depends: 'arch'/target_arch/'linker.ld')
else
  executable('kernel',
            sources,
            dependencies: [limine_proto, freestanding_hdrs],
            include_directories: archcommon_inc,
            c_args: c_args,
            link_args: linker_flags,
            link_depends: 'arch'/target_arch/'linker.ld')
endif
