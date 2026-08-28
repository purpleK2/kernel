# purplek2
A 64-bit kernel.

This repository exclusively holds the necessary to build the kernel. If you want to download/build an ISO, check out [pk2-bootstrap](https://github.com/purpleK2/pk2-bootstrap).

> [!WARNING]
> This branch is undergoing a (partial) rewrite. It may not build or run, or work at all as a project.

# Building the executable

Make sure you have `meson` and a C compiler (preferrably `gcc`) and linker (`ld`) installed on your system.
A bit granted in `$year_after_2025`, but an internet connection is needed to download libraries required by the kernel.

Setup the build directory:
```sh
meson setup build
```

Build the kernel:
```sh
meson compile -C build
# or
cd build
ninja
```

# If you want to...

- ... cross-compile for another platform, use [cross files](https://mesonbuild.com/Cross-compilation.html) and a cross-compiler (obviously). `host_machine.cpu_family()` is used as the target architecture.
- ... add cpu-specific code, x86_64 code is assembled with `nasm`. Other platforms will be assembled with `gcc`. Common arch headers and function declarations can go in `include/arch/common`, which is useful for functions that need to be implemented by all architectures (e.g. `_hcf`).
