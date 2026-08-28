project('limine', 'c', version: '12.5.2')

limine_include = include_directories('include')

limine_dep = declare_dependency(
  include_directories: limine_include
)

# Optional: export dependency for subproject fallbacks
meson.override_dependency('limine-protocol', limine_dep)
