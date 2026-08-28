project('freestanding_hdrs', 'c')

freestandinghdrs_include = include_directories('include')

freestandinghdrs_dep = declare_dependency(
  include_directories: freestandinghdrs_include
)

meson.override_dependency('freestanding_hdrs', freestandinghdrs_dep)
