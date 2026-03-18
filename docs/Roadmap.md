# [OS Codename] roadmap

The current status and the future of this OS.
A build (`.iso` file) will be released every time a Milestone is completed. The various milestones _might_ be updated over the course of time.

## 3rd Milestone

- GUI
  - [ ] lots of sketches and ideas

## 2nd Milestone

- [x] Port mlibc
  - [x] Basic sysdeps
  - [ ] Write smth with it
  - [ ] Dynamic Linking
  - [ ] Merge into upstream mlibc repo

- More filesystem stuff
  - [ ] ISO9660
- Hard drive setup:
  - [ ] FAT32 (boot partition)
  - [ ] EXTx (system partition)

- [x] Syscalls
- [x] ELF loading
- [ ] OS-specific toolchain
- [x] UNIX/POSIX compatibility layer
      (if someone wants to port `coreutils`, `bash`, whatever)

- [ ] Basic shell
  - [ ] Configuration System (registry)
  - [ ] Enviroment Variables
  - [x] Input and Ouput syscalls

- [ ] Video modes
  - [ ] Set better screen resolutions other than limine's default
  - [ ] Some kind of graphics API
  - [ ] Oh yeah, DOOM because we have to :3

## 1st Milestone

- [x] Bare bones (Limine and 64-bit kernel)

- [x] `printf` implementation (+ E9 port "debugging")

- [x] GDT
- [x] Interrupt handling (IDT, ISRs, IRQs)
- [x] PIC support

- PIT Driver
  - [x] Initialization
  - [x] PIT-supported sleep
- LAPIC/IOAPIC Initialization
  - [x] IRQ redirection to I/O APIC
  - [x] Interrupts work
  - [x] LAPIC timer init

- ACPI
  - uACPI implementation
    - [x] ACPI tables parsing
    - [x] the cool SSDT stuff
  - [x] Get RSDP/RSDT
  - [x] MADT (LAPIC initialization)
  - [x] HPET
  - [x] MCFG (PCIe devices parsing)

- Memory
  - [x] Get memory map
  - [x] Memory management
    - PMM
      - [x] Allocating/freeing page frames
    - VMM
      - [x] Paging
      - [x] Actual VMM stuff (allocating, freeing virtual memory regions)
    - [x] Kernel heap (`kmalloc`, `kfree`)

- [x] Driver interface (this thing took a long time)

- PCI/PCIe support
  - [x] PCI(e) devices parsing
  - [x] API for drivers

- [x] AHCI driver
      (we can read from the disk 🔥)

- File systems
  - ~~USTAR~~ CPIO
    - [x] Initial initrd creation
    - [x] File lookup
  - RAMFS
    - [x] Base structures
    - [x] File I/O (`open`, `read`, ...)
  - Virtual File System
    - [x] (Re)Design
    - [x] Initialization
    - [x] File I/O (`open`, `read`)
  - DevFS
    - [x] (Re)Design
    - [x] (Re)Implementation
  - ProcFS
    - [x] Design
    - [x] Implementation

- [x] Scheduler
  - [x] with threads and stuff
- [ ] SMP

- [x] Jump to userspace
