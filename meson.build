project('limine-meson',
        'c',
        version: '0.1',
        meson_version: '>= 1.1')

cc = meson.get_compiler('c')

target_arch = host_machine.cpu_family()
archcommon_inc = include_directories('include/arch/common')

summary('Build target', target_arch, section: 'Build information')

# user controllable C flags.
add_global_arguments('-pipe',
                     language : 'c')

limine_proto = dependency('limine-protocol', required: true)
freestanding_hdrs = dependency('freestanding_hdrs', required: true)

# build kernel executable.
subdir('src')
