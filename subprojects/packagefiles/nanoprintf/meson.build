project('nanoprintf', 'c', version: '0.8.0')

npf_inc = include_directories('.')

npf = declare_dependency(
  include_directories: npf_inc
)

# Optional: export dependency for subproject fallbacks
meson.override_dependency('nanoprintf', npf)
