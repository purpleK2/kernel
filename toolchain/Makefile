ARCH ?= x86_64
CPU_CORES ?= $(shell nproc)

BINUTILS_VERSION = 2.43
GCC_VERSION = 14.2.0

BINUTILS_URL = https://ftp.gnu.org/gnu/binutils/binutils-$(BINUTILS_VERSION).tar.xz
GCC_URL = https://ftp.gnu.org/gnu/gcc/gcc-$(GCC_VERSION)/gcc-$(GCC_VERSION).tar.xz

BINUTILS_SRC = binutils-$(BINUTILS_VERSION)
GCC_SRC = gcc-$(GCC_VERSION)

KERNEL_TARGET = $(ARCH)-elf
KERNEL_TOOLCHAIN_PREFIX = $(abspath kernel-toolchain)

KERNEL_BINUTILS_BUILD = binutils-build-kernel-$(BINUTILS_VERSION)
KERNEL_GCC_BUILD = gcc-build-kernel-$(GCC_VERSION)

USERSPACE_TARGET = $(ARCH)-purplek2
USERSPACE_TOOLCHAIN_PREFIX = $(abspath userspace-toolchain)
SYSROOT_DIR = $(abspath ../sysroot)

USERSPACE_BINUTILS_BUILD = binutils-build-userspace-$(BINUTILS_VERSION)
USERSPACE_GCC_BUILD = gcc-build-userspace-$(GCC_VERSION)

export PATH := $(KERNEL_TOOLCHAIN_PREFIX)/bin:$(USERSPACE_TOOLCHAIN_PREFIX)/bin:$(PATH)

.PHONY: all toolchain kernel-toolchain userspace-toolchain
.PHONY: kernel-toolchain-binutils kernel-toolchain-gcc
.PHONY: userspace-toolchain-binutils userspace-toolchain-gcc
.PHONY: clean-toolchain clean-toolchain-all clean-kernel-toolchain clean-userspace-toolchain
.PHONY: download-sources sysroot-dirs patch-sources

define PATCH_CONFIG_SUB
	@echo "=== Patching for purplek2 support ==="
	@echo "Patching config.sub..."
	sed -i 's/| sortix\*/| purplek2* | sortix*/g' $(1)/config.sub
	sed -i 's/| sortix\*/| purplek2* | sortix*/g' $(1)/*/config.sub 2>/dev/null || true
	sed -i 's/| sortix\*/| purplek2* | sortix*/g' $(1)/*/*/config.sub 2>/dev/null || true
	@echo "Patching bfd/config.bfd (binutils)..."
	sed -i 's/x86_64-\*-elf\*/x86_64-*-purplek2* | x86_64-*-elf*/g' $(1)/bfd/config.bfd 2>/dev/null || true
	@echo "Patching ld/configure.tgt (binutils)..."
	sed -i 's/x86_64-\*-elf\*/x86_64-*-purplek2* | x86_64-*-elf*/g' $(1)/ld/configure.tgt 2>/dev/null || true
	@echo "Patching gas/configure.tgt (binutils)..."
	sed -i 's/i386-\*-elf\*)/i386-*-purplek2*) fmt=elf ;;\n  i386-*-elf*)/g' $(1)/gas/configure.tgt 2>/dev/null || true
	@echo "Patching gcc/config.gcc (GCC)..."
	sed -i 's/x86_64-\*-elf\*)/x86_64-*-purplek2* | x86_64-*-elf*)/g' $(1)/gcc/config.gcc 2>/dev/null || true
	@echo "Patching libgcc/config.host (GCC)..."
	sed -i 's/x86_64-\*-elf\*/x86_64-*-purplek2* | x86_64-*-elf*/g' $(1)/libgcc/config.host 2>/dev/null || true
	@echo "Patching libstdc++-v3/crossconfig.m4 (GCC)..."
	sed -i 's/\*-\*-elf\*/*-*-purplek2* | *-*-elf*/g' $(1)/libstdc++-v3/crossconfig.m4 2>/dev/null || true
endef

all: toolchain

toolchain: kernel-toolchain userspace-toolchain

download-sources: $(BINUTILS_SRC).tar.xz $(GCC_SRC).tar.xz

$(BINUTILS_SRC).tar.xz:
	wget $(BINUTILS_URL)

$(GCC_SRC).tar.xz:
	wget $(GCC_URL)

kernel-toolchain: kernel-toolchain-binutils kernel-toolchain-gcc
	@echo "=== Kernel toolchain ($(KERNEL_TARGET)) built successfully ==="
	@echo "Location: $(KERNEL_TOOLCHAIN_PREFIX)"

kernel-toolchain-binutils: $(KERNEL_TOOLCHAIN_PREFIX)/bin/$(KERNEL_TARGET)-ld

$(KERNEL_TOOLCHAIN_PREFIX)/bin/$(KERNEL_TARGET)-ld: $(BINUTILS_SRC).tar.xz
	@echo "=== Building kernel binutils ==="
	tar -xf $(BINUTILS_SRC).tar.xz || true
	mkdir -p $(KERNEL_BINUTILS_BUILD)
	cd $(KERNEL_BINUTILS_BUILD) && \
		CFLAGS= ASMFLAGS= CC= CXX= LD= ASM= LINKFLAGS= LIBS= \
		../$(BINUTILS_SRC)/configure \
			--prefix="$(KERNEL_TOOLCHAIN_PREFIX)" \
			--target=$(KERNEL_TARGET) \
			--with-sysroot \
			--disable-nls \
			--disable-werror
	$(MAKE) -j$(CPU_CORES) -C $(KERNEL_BINUTILS_BUILD)
	$(MAKE) -C $(KERNEL_BINUTILS_BUILD) install

kernel-toolchain-gcc: $(KERNEL_TOOLCHAIN_PREFIX)/bin/$(KERNEL_TARGET)-gcc

$(KERNEL_TOOLCHAIN_PREFIX)/bin/$(KERNEL_TARGET)-gcc: $(KERNEL_TOOLCHAIN_PREFIX)/bin/$(KERNEL_TARGET)-ld $(GCC_SRC).tar.xz
	@echo "=== Building kernel GCC ==="
	tar -xf $(GCC_SRC).tar.xz || true
	mkdir -p $(KERNEL_GCC_BUILD)
	cd $(KERNEL_GCC_BUILD) && \
		CFLAGS= ASMFLAGS= CC= CXX= LD= ASM= LINKFLAGS= LIBS= \
		../$(GCC_SRC)/configure \
			--prefix="$(KERNEL_TOOLCHAIN_PREFIX)" \
			--target=$(KERNEL_TARGET) \
			--disable-nls \
			--enable-languages=c,c++ \
			--without-headers
	$(MAKE) -j$(CPU_CORES) -C $(KERNEL_GCC_BUILD) all-gcc all-target-libgcc
	$(MAKE) -C $(KERNEL_GCC_BUILD) install-gcc install-target-libgcc

userspace-toolchain: sysroot-dirs userspace-toolchain-binutils userspace-toolchain-gcc
	@echo "=== Userspace toolchain ($(USERSPACE_TARGET)) built successfully ==="
	@echo "Location: $(USERSPACE_TOOLCHAIN_PREFIX)"
	@echo "Sysroot:  $(SYSROOT_DIR)"

sysroot-dirs:
	@echo "=== Creating sysroot directory structure ==="
	mkdir -p $(SYSROOT_DIR)/usr/include
	mkdir -p $(SYSROOT_DIR)/usr/lib
	mkdir -p $(SYSROOT_DIR)/usr/bin
	mkdir -p $(SYSROOT_DIR)/lib
	mkdir -p $(SYSROOT_DIR)/bin

userspace-toolchain-binutils: $(USERSPACE_TOOLCHAIN_PREFIX)/bin/$(USERSPACE_TARGET)-ld

$(USERSPACE_TOOLCHAIN_PREFIX)/bin/$(USERSPACE_TARGET)-ld: $(BINUTILS_SRC).tar.xz sysroot-dirs
	@echo "=== Building userspace binutils ==="
	tar -xf $(BINUTILS_SRC).tar.xz || true
	$(call PATCH_CONFIG_SUB,$(BINUTILS_SRC))
	mkdir -p $(USERSPACE_BINUTILS_BUILD)
	cd $(USERSPACE_BINUTILS_BUILD) && \
		CFLAGS= ASMFLAGS= CC= CXX= LD= ASM= LINKFLAGS= LIBS= \
		../$(BINUTILS_SRC)/configure \
			--target=$(USERSPACE_TARGET) \
			--prefix="$(USERSPACE_TOOLCHAIN_PREFIX)" \
			--with-sysroot="$(SYSROOT_DIR)" \
			--disable-werror \
			--enable-default-execstack=no
	$(MAKE) -j$(CPU_CORES) -C $(USERSPACE_BINUTILS_BUILD)
	$(MAKE) -C $(USERSPACE_BINUTILS_BUILD) install

userspace-toolchain-gcc: $(USERSPACE_TOOLCHAIN_PREFIX)/bin/$(USERSPACE_TARGET)-gcc

$(USERSPACE_TOOLCHAIN_PREFIX)/bin/$(USERSPACE_TARGET)-gcc: $(USERSPACE_TOOLCHAIN_PREFIX)/bin/$(USERSPACE_TARGET)-ld $(GCC_SRC).tar.xz
	@echo "=== Building userspace GCC (bootstrap - without headers) ==="
	tar -xf $(GCC_SRC).tar.xz || true
	$(call PATCH_CONFIG_SUB,$(GCC_SRC))
	mkdir -p $(USERSPACE_GCC_BUILD)
	cd $(USERSPACE_GCC_BUILD) && \
		CFLAGS= ASMFLAGS= CC= CXX= LD= ASM= LINKFLAGS= LIBS= \
		../$(GCC_SRC)/configure \
			--target=$(USERSPACE_TARGET) \
			--prefix="$(USERSPACE_TOOLCHAIN_PREFIX)" \
			--with-sysroot="$(SYSROOT_DIR)" \
			--enable-languages=c \
			--disable-multilib \
			--disable-nls \
			--disable-shared \
			--disable-threads \
			--disable-libssp \
			--disable-libquadmath \
			--disable-libgomp \
			--disable-libatomic \
			--disable-libstdcxx \
			--disable-hosted-libstdcxx \
			--without-headers \
			--with-newlib
	$(MAKE) -j$(CPU_CORES) -C $(USERSPACE_GCC_BUILD) all-gcc
	$(MAKE) -j$(CPU_CORES) -C $(USERSPACE_GCC_BUILD) all-target-libgcc CFLAGS_FOR_TARGET="-O2 -mcmodel=large -mno-red-zone -fno-stack-protector -fno-exceptions"
	$(MAKE) -C $(USERSPACE_GCC_BUILD) install-gcc install-target-libgcc

.PHONY: userspace-toolchain-gcc-full userspace-toolchain-libstdcxx

userspace-toolchain-gcc-full:
	@echo "=== Rebuilding userspace GCC with full libc support ==="
	@if [ ! -f "$(SYSROOT_DIR)/usr/include/stdio.h" ]; then \
		echo "ERROR: mlibc headers not found in sysroot!"; \
		echo "Please install mlibc headers to $(SYSROOT_DIR)/usr/include first."; \
		exit 1; \
	fi
	rm -rf $(USERSPACE_GCC_BUILD)
	tar -xf $(GCC_SRC).tar.xz || true
	$(call PATCH_CONFIG_SUB,$(GCC_SRC))
	mkdir -p $(USERSPACE_GCC_BUILD)
	cd $(USERSPACE_GCC_BUILD) && \
		CFLAGS= ASMFLAGS= CC= CXX= LD= ASM= LINKFLAGS= LIBS= \
		../$(GCC_SRC)/configure \
			--target=$(USERSPACE_TARGET) \
			--prefix="$(USERSPACE_TOOLCHAIN_PREFIX)" \
			--with-sysroot="$(SYSROOT_DIR)" \
			--enable-languages=c,c++ \
			--enable-threads=posix \
			--disable-multilib \
			--enable-shared \
			--enable-host-shared \
			--disable-nls
	$(MAKE) -j$(CPU_CORES) -C $(USERSPACE_GCC_BUILD) all-gcc all-target-libgcc
	$(MAKE) -C $(USERSPACE_GCC_BUILD) install-gcc install-target-libgcc
	@echo "=== Full GCC rebuild complete ==="

userspace-toolchain-libstdcxx:
	@echo "=== Building libstdc++ (requires mlibc in sysroot) ==="
	@if [ ! -f "$(SYSROOT_DIR)/usr/include/stdio.h" ]; then \
		echo "ERROR: mlibc headers not found in sysroot!"; \
		echo "Please install mlibc headers to $(SYSROOT_DIR)/usr/include first."; \
		exit 1; \
	fi
	$(MAKE) -j$(CPU_CORES) -C $(USERSPACE_GCC_BUILD) all-target-libstdc++-v3
	$(MAKE) -C $(USERSPACE_GCC_BUILD) install-target-libstdc++-v3

clean-kernel-toolchain:
	rm -rf $(KERNEL_BINUTILS_BUILD) $(KERNEL_GCC_BUILD)

clean-userspace-toolchain:
	rm -rf $(USERSPACE_BINUTILS_BUILD) $(USERSPACE_GCC_BUILD)

clean-toolchain: clean-kernel-toolchain clean-userspace-toolchain
	rm -rf $(BINUTILS_SRC) $(GCC_SRC)

clean-toolchain-all: clean-toolchain
	rm -rf $(BINUTILS_SRC).tar.xz $(GCC_SRC).tar.xz
	rm -rf $(KERNEL_TOOLCHAIN_PREFIX) $(USERSPACE_TOOLCHAIN_PREFIX)