project('limine-meson',
        'c',
        version: '0.1',
        meson_version: '>= 1.1')

cc = meson.get_compiler('c')

target_arch = host_machine.cpu_family()
if (target_arch != 'x86_64')
  warning('Architectures other than x86_64 are not fully implemented and are prone to build errors.')
endif

inc = include_directories('include/arch/common', 'include/kernel')

summary('Build target', target_arch, section: 'Build information')

# user controllable C flags.
add_global_arguments('-pipe',
                     language : 'c')

limine_proto = dependency('limine-protocol', required: true)
freestanding_hdrs = dependency('freestanding_hdrs', required: true)

# build kernel executable.
subdir('src')
