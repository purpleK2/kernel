KERNEL_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)

USERSPACE_TARGET ?= x86_64-purplek2
USERSPACE_TOOLCHAIN_PREFIX ?= $(KERNEL_ROOT)/toolchain/userspace-toolchain
USERSPACE_SYSROOT ?= $(KERNEL_ROOT)/sysroot

CC = $(USERSPACE_TOOLCHAIN_PREFIX)/bin/$(USERSPACE_TARGET)-gcc
LD = $(USERSPACE_TOOLCHAIN_PREFIX)/bin/$(USERSPACE_TARGET)-ld

FASM ?= fasm

USERSPACE_CFLAGS = \
	--sysroot=$(USERSPACE_SYSROOT) \
	-O2 \
	-g \
	-Wall \
	-Wextra \
	-static

USERSPACE_CFLAGS_BOOTSTRAP = \
	--sysroot=$(USERSPACE_SYSROOT) \
	-isystem $(USERSPACE_SYSROOT)/usr/include \
	-ffreestanding \
	-nostdlib \
	-fno-stack-protector \
	-fPIE \
	-O2 \
	-g \
	-Wall \
	-Wextra

USERSPACE_LDFLAGS = \
	--sysroot=$(USERSPACE_SYSROOT) \
	-static \
	$(USERSPACE_SYSROOT)/usr/lib/Scrt1.o \
	$(USERSPACE_SYSROOT)/usr/lib/crti.o \
	-lc \
	$(USERSPACE_SYSROOT)/usr/lib/crtn.o

USERSPACE_LDFLAGS_BOOTSTRAP = \
	-nostdlib \
	-pie \
	--entry=_start

MLIBC_INSTALLED := $(shell test -f $(USERSPACE_SYSROOT)/usr/include/stdio.h && echo yes || echo no)

ifeq ($(MLIBC_INSTALLED),yes)
    CFLAGS ?= $(USERSPACE_CFLAGS)
    LDFLAGS_BASE ?= $(USERSPACE_LDFLAGS)
else
    CFLAGS ?= $(USERSPACE_CFLAGS_BOOTSTRAP)
    LDFLAGS_BASE ?= $(USERSPACE_LDFLAGS_BOOTSTRAP)
endif

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.asm
	$(FASM) $< $@
