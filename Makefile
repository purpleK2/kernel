ARCH=x86_64
TARGET_BASE=$(ARCH)

KERNEL_TARGET=$(TARGET_BASE)-elf
KERNEL_TOOLCHAIN_PREFIX=$(abspath toolchain/kernel-toolchain)

USERSPACE_TARGET=$(TARGET_BASE)-purplek2
USERSPACE_TOOLCHAIN_PREFIX=$(abspath toolchain/userspace-toolchain)
SYSROOT_DIR=$(abspath sysroot)

TARGET=$(KERNEL_TARGET)
TOOLCHAIN_PREFIX=$(KERNEL_TOOLCHAIN_PREFIX)

export PATH:=$(KERNEL_TOOLCHAIN_PREFIX)/bin:$(USERSPACE_TOOLCHAIN_PREFIX)/bin:$(PATH)

export USERSPACE_CC=$(USERSPACE_TARGET)-gcc
export USERSPACE_LD=$(USERSPACE_TARGET)-ld
export USERSPACE_SYSROOT=$(SYSROOT_DIR)
export USERSPACE_TARGET

OS_CODENAME=kernel-v0

LIBS_DIR=libs

SRC_DIR=src
ARCH_DIR=$(SRC_DIR)/arch/$(TARGET_BASE)
KERNEL_SRC_DIR=$(SRC_DIR)/kernel
ISO_DIR=iso

BUILD_DIR=build

OBJS_DIR=$(BUILD_DIR)/objs
INITRD_DIR=sysroot
INITRD=initrd.cpio

KCONFIG_CONFIG = .config
KCONFIG_DEPS = Kconfig
KCONFIG_AUTOCONF = $(KERNEL_SRC_DIR)/autoconf.h

MODULE_DIRS := $(shell find modules -mindepth 1 -maxdepth 4 -type d)
MODULES := $(foreach d,$(MODULE_DIRS),$(wildcard $(d)/*.km))

APPS_DIRS := $(shell find apps -mindepth 1 -maxdepth 1 -type d)

QEMU_FLAGS = -m 2G \
    		 -debugcon stdio \
    		 -M q35 \
    		 -smp 2 \
			 -enable-kvm 

QEMU_FLAGS_GDB = -m 2G \
    		 -debugcon file:qemu_gdb.log \
    		 -M q35 \
    		 -smp 2 \
    		 -netdev tap,id=net0,ifname=tap0,script=no,downscript=no \
		 	 -device rtl8139,netdev=net0,mac=52:54:00:12:34:56

# Nuke built-in rules and variables.
override MAKEFLAGS += -rR --no-print-directory

# This is the name that our final kernel executable will have.
override KERNEL := kernel.elf

# Convenience macro to reliably declare user overridable variables.
define DEFAULT_VAR =
	ifeq ($(origin $1),default)
		override $(1) := $(2)
	endif
	ifeq ($(origin $1),undefined)
		override $(1) := $(2)
	endif
endef

# It is suggested to use a custom built cross toolchain to build a kernel.
override DEFAULT_KCC := $(TARGET)-gcc
$(eval $(call DEFAULT_VAR,KCC,$(DEFAULT_KCC)))

# Same thing for "ld" (the linker).
override DEFAULT_KLD := $(TARGET)-ld
$(eval $(call DEFAULT_VAR,KLD,$(DEFAULT_KLD)))

# User controllable C flags.
override DEFAULT_KCFLAGS := \
	-g \
	-O0 \
	-pipe \
	-I $(SRC_DIR)/lib \
	-I $(KERNEL_SRC_DIR) \
	-I $(KERNEL_SRC_DIR)/system \
	-I $(KERNEL_SRC_DIR)/acpi \
	-I $(ARCH_DIR) \
	-D UACPI_KERNEL_INITIALIZATION \
	-D UACPI_FORMATTED_LOGGING \
	-D CHAR_BIT=8

$(eval $(call DEFAULT_VAR,KCFLAGS,$(DEFAULT_KCFLAGS)))

# User controllable C preprocessor flags. We set none by default.
override DEFAULT_KCPPFLAGS :=
$(eval $(call DEFAULT_VAR,KCPPFLAGS,$(DEFAULT_KCPPFLAGS)))

# User controllable assembler flags.
override DEFAULT_KNASMFLAGS :=
$(eval $(call DEFAULT_VAR,KNASMFLAGS,$(DEFAULT_KNASMFLAGS)))

# User controllable linker flags. We set none by default.
override DEFAULT_KLDFLAGS := -Map=$(BUILD_DIR)/kernel.map
$(eval $(call DEFAULT_VAR,KLDFLAGS,$(DEFAULT_KLDFLAGS)))

# Internal C flags that should not be changed by the user.
override KCFLAGS += \
	-Wall \
	-Wextra \
	-std=gnu11 \
	-ffreestanding \
	-fno-stack-protector \
	-fno-stack-check \
	-fno-lto \
	-fPIE \
	-fno-PIC \
	-m64 \
	-march=x86-64 \
	-mno-80387 \
	-mno-mmx \
	-mno-red-zone \
	-mcmodel=kernel

# Internal C preprocessor flags that should not be changed by the user.
override KCPPFLAGS := \
	$(KCPPFLAGS) \
	-MMD \
	-MP

# Internal linker flags that should not be changed by the user.
override KLDFLAGS += \
	-m elf_x86_64 \
	-nostdlib \
	-static \
	-z max-page-size=0x1000 \
	-T $(SRC_DIR)/linker.ld

# Internal assembler flags that should not be changed by the user.
override KNASMFLAGS +=

# Create required directories
$(shell mkdir -p $(BUILD_DIR) $(OBJS_DIR) $(ISO_DIR))

# Use "find" to glob all *.c, *.S, and *.asm files in the tree and obtain the
# object and header dependency file names.
override CFILES := $(shell cd $(SRC_DIR) && find -L * -type f -name '*.c')
override ASFILES := $(shell cd $(SRC_DIR) && find -L * -type f -name '*.S')
override NASMFILES := $(shell cd $(SRC_DIR) && find -L * -type f -name '*.asm')
override OBJ := $(addprefix $(OBJS_DIR)/,$(CFILES:.c=.c.o) $(ASFILES:.S=.S.o) $(NASMFILES:.asm=.asm.o))
override HEADER_DEPS := $(addprefix $(OBJS_DIR)/,$(CFILES:.c=.c.d) $(ASFILES:.S=.S.d))

# Default target.
.PHONY: all limine_build toolchain libs modules

all: $(OS_CODENAME).iso

all-hdd: $(OS_CODENAME).hdd

# Define the ISO image file as an explicit target with dependencies
$(OS_CODENAME).iso: modules $(ISO_DIR)/$(KERNEL) $(ISO_DIR)/$(INITRD) $(ISO_DIR)/boot/limine.conf $(ISO_DIR)/boot/limine/limine-bios-cd.bin $(ISO_DIR)/boot/limine/limine-uefi-cd.bin $(ISO_DIR)/EFI/BOOT/BOOTX64.EFI $(ISO_DIR)/bg.png limine_build $(ISO_DIR)/boot/limine/limine-bios.sys
	@# Create the bootable ISO.
	xorriso -as mkisofs -b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		$(ISO_DIR) -o $@

	@# Install Limine stage 1 and 2 for legacy BIOS boot.
	./$(LIBS_DIR)/limine/limine bios-install $@
	@echo "--> ISO:	" $@

$(OS_CODENAME).hdd: modules $(BUILD_DIR)/$(INITRD) $(BUILD_DIR)/$(KERNEL) limine_build
	rm -f $@
	dd if=/dev/zero bs=1M count=0 seek=64 of=$@
	
	sgdisk $@ -n 1:2048 -t 1:ef00 -m 1
	@# fix for "The kernel is still using the old partition table"
	partprobe $@

	mformat -i $(OS_CODENAME).hdd@@1M
	./$(LIBS_DIR)/limine/limine bios-install $(OS_CODENAME).hdd

	mmd -i $@@@1M ::/EFI ::/EFI/BOOT ::/boot ::/boot/limine
	mcopy -i $@@@1M $(BUILD_DIR)/$(KERNEL) ::/
	mcopy -i $@@@1M $(BUILD_DIR)/$(INITRD) ::/
	mcopy -i $@@@1M $(SRC_DIR)/limine.conf ::/boot/limine
	mcopy -i $@@@1M $(SRC_DIR)/bg.png ::/

	mcopy -i $@@@1M $(LIBS_DIR)/limine/limine-bios.sys ::/boot/limine
	mcopy -i $@@@1M $(LIBS_DIR)/limine/BOOTX64.EFI ::/EFI/BOOT
	mcopy -i $@@@1M $(LIBS_DIR)/limine/BOOTIA32.EFI ::/EFI/BOOT

# Copy kernel to ISO directory
$(ISO_DIR)/$(KERNEL): $(BUILD_DIR)/$(KERNEL)
	cp -v $< $@

# Copy initramfs to ISO directory
$(ISO_DIR)/$(INITRD): $(BUILD_DIR)/$(INITRD)
	cp -v $< $@

$(ISO_DIR)/boot/limine.conf: $(SRC_DIR)/limine.conf
	mkdir -p "$$(dirname $@)"
	cp -v $< $@

# Copy Limine bootloader files
$(ISO_DIR)/boot/limine/limine-bios-cd.bin: $(LIBS_DIR)/limine/limine-bios-cd.bin
	mkdir -p "$$(dirname $@)"
	cp -v $< $@

$(ISO_DIR)/boot/limine/limine-uefi-cd.bin: $(LIBS_DIR)/limine/limine-uefi-cd.bin
	mkdir -p "$$(dirname $@)"
	cp -v $< $@

$(ISO_DIR)/boot/limine/limine-bios.sys: $(LIBS_DIR)/limine/limine-bios.sys
	mkdir -p "$$(dirname $@)"
	cp -v $< $@

$(ISO_DIR)/EFI/BOOT/BOOTX64.EFI: $(LIBS_DIR)/limine/BOOTX64.EFI
	mkdir -p "$$(dirname $@)"
	cp -v $< $@

$(ISO_DIR)/EFI/BOOT/BOOTIA32.EFI: $(LIBS_DIR)/limine/BOOTIA32.EFI
	mkdir -p "$$(dirname $@)"
	cp -v $< $@

$(ISO_DIR)/bg.png: $(SRC_DIR)/bg.png
	cp -v $< $@

# Setup bootloader files
bootloader-files: $(ISO_DIR)/boot/limine/limine-bios-cd.bin $(ISO_DIR)/boot/limine/limine-uefi-cd.bin $(ISO_DIR)/EFI/BOOT/BOOTX64.EFI $(ISO_DIR)/EFI/BOOT/BOOTIA32.EFI

limine_build: $(LIBS_DIR)/limine/limine

$(LIBS_DIR)/limine/limine:
	@# Build "limine" utility
	make -C $(LIBS_DIR)/limine

modules:
	@mkdir -p $(SYSROOT_DIR)/kmod
	@for dir in $(MODULE_DIRS); do \
		echo "--> Building module in $$dir"; \
		$(MAKE) -C $$dir; \
		for km in $$dir/*.km; do \
			if [ -f $$km ]; then \
				cp -v $$km $(SYSROOT_DIR)/kmod/; \
			fi; \
		done; \
	done

.PHONY: apps
apps:
	@mkdir -p $(SYSROOT_DIR)/bin
	@for dir in $(APPS_DIRS); do \
		echo "--> Building app in $$dir"; \
		$(MAKE) -C $$dir; \
		for f in $$dir/*; do \
			if [ -f "$$f" ] && \
			   readelf -h "$$f" 2>/dev/null | grep -Eq "Type:[[:space:]]*(EXEC|DYN)"; then \
				cp -v "$$f" $(SYSROOT_DIR)/bin; \
			fi; \
		done; \
	done
	
libs:
	@./libs/clone_repos.sh libs/
	@./libs/get_deps.sh src/kernel libs/
	@$(MAKE) limine_build
	@git clone --depth 1 https://github.com/purpleK2/mlibc.git libc/mlibc

TOOLCHAIN := $(CURDIR)/toolchain/userspace-toolchain/bin

.PHONY: libc
libc:
	cd libc/mlibc && \
	mkdir -p headers-build && \
	meson setup --cross-file=../purpleK2-cross.txt --prefix=/usr -Dheaders_only=true \
		headers-build && \
	MLIBC_RTLD_DEBUG=1 MLIBC_RTLD_DEBUG_VERBOSE=1 DESTDIR=$(USERSPACE_SYSROOT) meson install -C headers-build && \
	MLIBC_RTLD_DEBUG=1 MLIBC_RTLD_DEBUG_VERBOSE=1 PATH=$(TOOLCHAIN):$$PATH meson setup \
		--cross-file=../purpleK2-cross.txt \
		--prefix=/usr \
		-Ddefault_library=static \
		-Dno_headers=true \
		build && \
	MLIBC_RTLD_DEBUG=1 MLIBC_RTLD_DEBUG_VERBOSE=1 PATH=$(TOOLCHAIN):$$PATH ninja -C build
	cp -v libc/mlibc/build/*.a $(USERSPACE_SYSROOT)/usr/lib/
	cp -v libc/mlibc/build/sysdeps/purpleK2/*.o $(USERSPACE_SYSROOT)/usr/lib/

# Create initrd image
$(BUILD_DIR)/$(INITRD): modules apps
		cd $(INITRD_DIR) && \
		find . -type f | cpio -H newc -o > ../$(BUILD_DIR)/$(INITRD) && \
		cd ..

# Link rules for the final kernel executable.
$(BUILD_DIR)/$(KERNEL): $(SRC_DIR)/linker.ld $(OBJ)
	mkdir -p "$$(dirname $@)"
	$(KLD) $(OBJ) $(KLDFLAGS) -o $@
	@echo "--> Built:	" $@

# Include header dependencies.
-include $(HEADER_DEPS)

# Compilation rules for *.c files.
$(OBJS_DIR)/%.c.o: $(SRC_DIR)/%.c
	mkdir -p "$$(dirname $@)"
	$(KCC) $(KCFLAGS) $(KCPPFLAGS) -c $< -o $@
	@echo "--> Compiled:	" $<

# Compilation rules for *.S files.
$(OBJS_DIR)/%.S.o: $(SRC_DIR)/%.S
	mkdir -p "$$(dirname $@)"
	$(KCC) $(KCFLAGS) $(KCPPFLAGS) -c $< -o $@
	@echo "--> Assembled:	" $<

# Compilation rules for *.asm (nasm) files.
$(OBJS_DIR)/%.asm.o: $(SRC_DIR)/%.asm
	mkdir -p "$$(dirname $@)"
	fasm $< $@
	@echo "--> Assembled:	" $<

run: $(OS_CODENAME).iso
	qemu-system-$(ARCH) \
		$(QEMU_FLAGS) \
		-cdrom $<

run-uefi: $(OS_CODENAME).iso edk2-ovmf
	qemu-system-$(ARCH) \
		$(QEMU_FLAGS) \
		-bios edk2-ovmf/ovmf-code-$(ARCH).fd \
		-cdrom $<

run-hdd: $(OS_CODENAME).hdd
	qemu-system-$(ARCH) \
		$(QEMU_FLAGS) \
		-hda $<

run-hdd-uefi: $(OS_CODENAME).hdd edk2-ovmf
	qemu-system-$(ARCH) \
		$(QEMU_FLAGS) \
		-bios edk2-ovmf/ovmf-code-$(ARCH).fd \
		-hda $<

run-wsl: $(OS_CODENAME).iso
	qemu-system-$(ARCH).exe \
		$(QEMU_FLAGS) \
		-cdrom $< \
		-accel whpx

run-wsl-uefi: $(OS_CODENAME).iso edk2-ovmf
	qemu-system-$(ARCH).exe \
		$(QEMU_FLAGS) \
		-cdrom $< \
		-bios edk2-ovmf/ovmf-code-$(ARCH).fd \
		-accel whpx

run-wsl-hdd: $(OS_CODENAME).hdd
	qemu-system-$(ARCH).exe \
		$(QEMU_FLAGS) \
		-hda $< \
		-accel whpx

run-wsl-hdd-uefi: $(OS_CODENAME).hdd edk2-ovmf
	qemu-system-$(ARCH).exe \
		$(QEMU_FLAGS) \
		-hda $< \
		-bios edk2-ovmf/ovmf-code-$(ARCH).fd \
		-accel whpx

edk2-ovmf:
	./libs/get_deps.sh $(SRC_DIR)/kernel libs

menuconfig:
	kconfig-mconf $(KCONFIG_DEPS)
	python scripts/kconfig.py

defconfig_release:
	kconfig-conf --defconfig=build_configs/default_release $(KCONFIG_DEPS)
	python scripts/kconfig.py

allyesconfig:
	kconfig-conf --allyesconfig $(KCONFIG_DEPS)
	python scripts/kconfig.py

debug: $(OS_CODENAME).iso
	gdb -x debug_scripts/iso_bios.gdb $(BUILD_DIR)/$(KERNEL)

debug-remote: $(OS_CODENAME).iso
	qemu-system-$(ARCH) $(QEMU_FLAGS_GDB) -cdrom $< -S -gdb tcp::1234 -daemonize

debug-uefi: $(OS_CODENAME).iso
	gdb -x debug_scripts/iso_uefi.gdb $(BUILD_DIR)/$(KERNEL)

debug-hdd: $(OS_CODENAME).hdd
	gdb -x debug_scripts/hdd_bios.gdb $(BUILD_DIR)/$(KERNEL)

debug-hdd-uefi: $(OS_CODENAME).hdd
	gdb -x debug_scripts/hdd_uefi.gdb $(BUILD_DIR)/$(KERNEL)

# Remove object files.
.PHONY: clean distclean

clean:
	rm -rf $(OS_CODENAME).iso $(OS_CODENAME).hdd

distclean:
	rm -rf $(BUILD_DIR) $(ISO_DIR) *.iso *.hdd
