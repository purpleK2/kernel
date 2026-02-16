#include "kernel.h"
#include "dev/tty/tty.h"
#include "dev/tty/vt.h"
#include "loader/elf/elfloader.h"
#include "karg.h"
#include "pci/pci.h"

#include <autoconf.h>

#include <arch.h>
#include <cpu.h>
#include <interrupts/isr.h>
#include <io.h>
#include <limine.h>

#include <loader/module/module_loader.h>

#include <acpi/acpi.h>

#include <dev/device.h>
#include <dev/display/fb/fbdev.h>
#include <dev/fs/initrd.h>
#include <dev/port/e9/e9.h>
#include <dev/port/parallel/parallel.h>
#include <dev/port/serial/serial.h>
#include <dev/std/helper.h>

#include <fs/cpio/newc.h>
#include <fs/devfs/devfs.h>
#include <fs/file_io.h>
#include <fs/procfs/procfs.h>
#include <fs/ramfs/ramfs.h>
#include <fs/vfs/vfs.h>

#include <memory/heap/kheap.h>
#include <memory/pmm/pmm.h>
#include <memory/vmm/vflags.h>
#include <memory/vmm/vmm.h>
#include <paging/paging.h>

#include <pcie/pcie.h>

#include <scheduler/scheduler.h>

#include <system/sleep.h>

#include <smp/ipi.h>
#include <smp/smp.h>

#include <tables/hpet.h>

#include <terminal/psf.h>
#include <terminal/terminal.h>

#include <tsc/tsc.h>

#include <util/assert.h>
#include <util/macro.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define INITRD_FILE  "initrd.cpio"
#define INITRD_PATH  "/" INITRD_FILE
#define INITRD_MOUNT "/initrd"

USED SECTION(".requests") static volatile uint64_t limine_base_revision[] =
    LIMINE_BASE_REVISION(4);

USED SECTION(".requests") static volatile struct limine_framebuffer_request
    framebuffer_request = {.id = LIMINE_FRAMEBUFFER_REQUEST_ID, .revision = 0};

USED SECTION(".requests") static volatile struct limine_memmap_request
    memmap_request = {.id = LIMINE_MEMMAP_REQUEST_ID, .revision = 0};

USED SECTION(".requests") static volatile struct limine_paging_mode_request
    paging_mode_request = {.id = LIMINE_PAGING_MODE_REQUEST_ID, .revision = 0};

USED SECTION(".requests") static volatile struct limine_hhdm_request
    hhdm_request = {.id = LIMINE_HHDM_REQUEST_ID, .revision = 0};

USED SECTION(
    ".requests") static volatile struct limine_executable_address_request
    kernel_address_request = {.id       = LIMINE_EXECUTABLE_ADDRESS_REQUEST_ID,
                              .revision = 0};

USED SECTION(".requests") static volatile struct limine_rsdp_request
    rsdp_request = {.id = LIMINE_RSDP_REQUEST_ID, .revision = 0};

USED SECTION(".requests") static volatile struct limine_module_request
    module_request = {.id = LIMINE_MODULE_REQUEST_ID, .revision = 0};

USED SECTION(".requests") static volatile struct limine_firmware_type_request
    firmware_type_request = {.id       = LIMINE_FIRMWARE_TYPE_REQUEST_ID,
                             .revision = 0};

USED SECTION(".requests") static volatile struct limine_mp_request
    smp_request = {.id = LIMINE_MP_REQUEST_ID, .revision = 0};

USED SECTION(".requests") static volatile struct limine_executable_file_request
    kernel_file_request = {.id       = LIMINE_EXECUTABLE_FILE_REQUEST_ID,
                           .revision = 0};

USED SECTION(".requests") static volatile struct limine_executable_cmdline_request
    kernel_cmdline_request = {
        .id       = LIMINE_EXECUTABLE_CMDLINE_REQUEST_ID,
        .revision = 0};

USED SECTION(".requests_start_marker") static volatile uint64_t
    limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

USED SECTION(".requests_end_marker") static volatile uint64_t
    limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

struct limine_framebuffer *framebuffer;
struct limine_memmap_response *memmap_response;
struct limine_memmap_entry *memmap_entry;
struct limine_hhdm_response *hhdm_response;
struct limine_executable_address_response *kernel_address_response;
struct limine_paging_mode_response *paging_mode_response;
struct limine_rsdp_response *rsdp_response;
struct limine_module_response *module_response;
struct limine_file *kernel_file;
struct limine_executable_cmdline_response *kernel_cmdline_response;

struct bootloader_data limine_parsed_data;

struct bootloader_data *get_bootloader_data() {
    return &limine_parsed_data;
}

vmc_t *kvmc;

static int karg_init_exec(const char *value) {
    if (value == NULL) {
        limine_parsed_data.init_exec = NULL;
        return -1;
    }

    debugf_debug("Init Executable Path: %s\n", value);

    limine_parsed_data.init_exec = (char *)value;
    return 0;
}

// kernel main function
void kstart(void) {
    _disable_interrupts();
    // Ensure the bootloader actually understands our base revision (see spec).
    if (LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision) == false) {
        _hcf();
    }

    // Ensure we got a framebuffer.
    if (framebuffer_request.response == NULL ||
        framebuffer_request.response->framebuffer_count < 1) {
        debugf_debug("No framebuffer!\n");
        _hcf();
    }

    // Fetch the first framebuffer.
    framebuffer = framebuffer_request.response->framebuffers[0];
    limine_parsed_data.framebuffer = framebuffer;

    psfLoadDefaults();
    _term_init();

    set_screen_bg_fg(DEFAULT_BG, DEFAULT_FG); // black-ish, white-ish
    clearscreen();

    limine_parsed_data.kernel_file_data =
        kernel_file_request.response->executable_file->address;
    limine_parsed_data.kernel_file_size =
        kernel_file_request.response->executable_file->size;

    limine_parsed_data.karg_cmdline =
        kernel_cmdline_request.response
            ? kernel_cmdline_request.response->cmdline
            : NULL;

    limine_parsed_data.bootstrap_cpu_id =
        smp_request.response
            ? smp_request.response->bsp_lapic_id
            : 0;

    karg_register("init", karg_init_exec, 0);

    if (limine_parsed_data.karg_cmdline) {
        karg_parse(limine_parsed_data.karg_cmdline);
    }

    arch_base_init();

    uint64_t system_startup_time;
    mark_time(&system_startup_time);
    system_startup_time -=
        1000; // add one second because of the TSC calibration

    char *FIRMWARE_TYPE;

    if (firmware_type_request.response != NULL) {
        uint64_t firmware_type = firmware_type_request.response->firmware_type;
        switch (firmware_type) {
        case LIMINE_FIRMWARE_TYPE_X86BIOS:
            FIRMWARE_TYPE = "X86BIOS";
            break;

        case LIMINE_FIRMWARE_TYPE_EFI32:
            FIRMWARE_TYPE = "UEFI32";
            break;

        case LIMINE_FIRMWARE_TYPE_EFI64:
            FIRMWARE_TYPE = "UEFI64";
            break;

        default:
            FIRMWARE_TYPE = "No firmware type found!";
            break;
        }
    } else {
        FIRMWARE_TYPE = "No firmware type found!";
    }

    debugf_debug("%s\n", FIRMWARE_TYPE);

    debugf_debug("Current video mode is: %dx%d address: %p\n\n",
                 framebuffer->width, framebuffer->height, framebuffer->address);

    assert(kernel_address_request.response);
    kernel_address_response = kernel_address_request.response;
    limine_parsed_data.kernel_base_physical =
        kernel_address_response->physical_base;
    limine_parsed_data.kernel_base_virtual =
        kernel_address_response->virtual_base;
    debugf_debug("Kernel address: (phys)%llx (virt)%llx\n\n",
                 limine_parsed_data.kernel_base_physical,
                 limine_parsed_data.kernel_base_virtual);

    debugf_debug("Kernel sections:\n");
    debugf_debug("\tkernel_start: %p\n", &__kernel_start);
    debugf_debug("\tkernel_text_start: %p; kernel_text_end: %p\n",
                 &__kernel_text_start, &__kernel_text_end);
    debugf_debug("\tkernel_rodata_start: %p; kernel_rodata_end: %p\n",
                 &__kernel_rodata_start, &__kernel_rodata_end);
    debugf_debug("\tkernel_data_start: %p; kernel_data_end: %p\n",
                 &__kernel_data_start, &__kernel_data_end);
    debugf_debug("\tlimine_requests_start: %p; limine_requests_end: %p\n",
                 &__limine_reqs_start, &__limine_reqs_end);
    debugf_debug("\tkernel_end: %p\n", &__kernel_end);

    assert(memmap_request.response || memmap_request.response->entry_count > 0);
    kernel_file = kernel_file_request.response->executable_file;

    limine_parsed_data.kernel_file_data = kernel_file->address;
    limine_parsed_data.kernel_file_size = kernel_file->size;

    memmap_response = memmap_request.response;

    limine_parsed_data.limine_memory_map  = memmap_response->entries;
    limine_parsed_data.memmap_entry_count = memmap_response->entry_count;

    // Load limine's memory map into OUR struct
    limine_parsed_data.usable_entry_count = 0;
    for (uint64_t i = 0; i < limine_parsed_data.memmap_entry_count; i++) {
        memmap_entry = limine_parsed_data.limine_memory_map[i];

        if (memmap_entry->type == LIMINE_MEMMAP_USABLE) {
            limine_parsed_data.memory_usable_total += memmap_entry->length;
            limine_parsed_data.usable_entry_count++;
        }

        debugf_debug(
            "Entry n. %lld; Region start: %llx; length: %llx; type: %s\n", i,
            memmap_entry->base, memmap_entry->length,
            memory_block_type[memmap_entry->type]);
    }

    limine_parsed_data.memory_total =
        limine_parsed_data
            .limine_memory_map[limine_parsed_data.memmap_entry_count - 1]
            ->base +
        limine_parsed_data
            .limine_memory_map[limine_parsed_data.memmap_entry_count - 1]
            ->length -
        limine_parsed_data
            .limine_memory_map[limine_parsed_data.memmap_entry_count - 1]
            ->length;

    assert(hhdm_request.response);
    hhdm_response = hhdm_request.response;

    limine_parsed_data.hhdm_offset = hhdm_response->offset;
    debugf_debug("Higher Half Direct Map offset: %llx\n",
                 limine_parsed_data.hhdm_offset);

    pmm_init(memmap_response);
    kprintf_ok("Initialized PMM\n");

    assert(paging_mode_request.response);
    paging_mode_response = paging_mode_request.response;

    if (paging_mode_response->mode < paging_mode_request.min_mode ||
        paging_mode_response->mode > paging_mode_request.max_mode) {
        kprintf_panic("%lluth paging mode is not supported!\n",
                      paging_mode_response->mode);
        _hcf();
    }
    debugf_debug("%lluth level paging is available\n",
                 4 + paging_mode_response->mode);

    if (!check_pae()) {
        debugf_debug("PAE is not available\n");
    } else {
        debugf_debug("PAE is available\n");
    }

    debugf_debug("Initializing paging\n");
    // kernel PML4 table
    uint64_t *kernel_pml4 = (uint64_t *)pmm_alloc_page();
    paging_init((uint64_t *)PHYS_TO_VIRTUAL(kernel_pml4));

    kvmc = vmc_init((uint64_t *)PHYS_TO_VIRTUAL(kernel_pml4), VMO_KERNEL_RW);
    vmm_init(kvmc);
    pmm_init_refcount();
    vmc_switch(kvmc);
    set_kernel_vmc(kvmc);
    kprintf_ok("Initialized VMM\n");

    change_to_kernel_pml4_on_int = 1;

    kmalloc_init();

    debugf_debug("Malloc Test:\n");
    void *ptr1 = kmalloc(0xA0);
    debugf_debug("[1] kmalloc(0xA0) @ %p\n", ptr1);
    void *ptr2 = kmalloc(0xA3B0);
    debugf_debug("[2] kmalloc(0xA3B0) @ %p\n", ptr2);
    kfree(ptr1);
    kfree(ptr2);
    ptr1 = kmalloc(0xF00);
    debugf_debug("[3] kmalloc(0xF00) @ %p\n", ptr1);
    kfree(ptr1);
    kprintf_ok("kheap init done\n");

    assert(rsdp_request.response);
    rsdp_response                         = rsdp_request.response;
    limine_parsed_data.rsdp_table_address = (uint64_t *)rsdp_response->address;
    debugf_debug("Address of RSDP: %p\n",
                 limine_parsed_data.rsdp_table_address);

    if (uacpi_init() == 0) {
        kprintf_ok("uACPI initialized successfully!!\n");
    } else {
        kprintf_panic("Some errors occured during uACPI initialization.");
        debugf_panic(
            "Some errors occured during uACPI initialization. Halting...\n");

        _hcf();
    }

#if defined(__x86_64__)
#include <apic/ioapic/ioapic.h>
#include <apic/lapic/lapic.h>

    if (check_apic()) {
        debugf_debug("APIC is supported\n");

        lapic_init();
        ioapic_init();

        kprintf_ok("LAPIC + IOAPIC init done\n");
    } else {
        debugf_debug("APIC is not supported. Going on with legacy PIC\n");
    }

// hpet_init();
// kprintf_ok("HPET initialized\n");
#endif

    char *cpu_name   = kmalloc(49);
    char *cpu_vendor = kmalloc(13);
    get_cpu_name(cpu_name);
    get_cpu_vendor(cpu_vendor);
    kprintf("CPU: %s %s @ %llu MHz\n", cpu_vendor, cpu_name,
            tsc_frequency / 1000 / 1000);
    kfree(cpu_vendor);
    kfree(cpu_name);

    // hypervisor
    if (check_hypervisor()) {
        char hv[13];
        if (get_hypervisor(hv) == 0) {
            kprintf("Running on hypervisor: %s\n\n", hv);
        }
    }

    kprintf("Total Memory: 0x%llx (%lu MBytes)\n",
            limine_parsed_data.memory_total,
            limine_parsed_data.memory_total / 0x100000);

    kprintf("Total available Memory: 0x%llx (%lu MBytes)\n\n",
            limine_parsed_data.memory_usable_total,
            limine_parsed_data.memory_usable_total / 0x100000);

    assert(smp_request.response);

    limine_parsed_data.cpu_count = smp_request.response->cpu_count;
    limine_parsed_data.cpus      = smp_request.response->cpus;

    // first scheduler, then FS
    init_scheduler();
    sleep_init();

    if (!module_request.response) {
        kprintf_warn("No modules loaded.\n");
    }

    module_response = module_request.response;

    struct limine_file *initrd = NULL;

    for (uint64_t module = 0; module < module_response->module_count;
         module++) {
        struct limine_file *limine_module = module_response->modules[module];
        kprintf_info("Module %s loaded\n", limine_module->path);

        if (strcmp(INITRD_PATH, limine_module->path) == 0) {
            kprintf_ok("Found initrd image\n");
            initrd = limine_module;
        }
    }

    assert(initrd);

    kprintf_info("Initrd loaded at address %p\n", initrd->address);

    cpio_t cpio;
    memset(&cpio, 0, sizeof(cpio_t));
    assert(cpio_fs_parse(&cpio, initrd->address, initrd->size) == 0);

    // register file system types
    ramfs_init();
    vfs_mount(NULL, "ramfs", "/", NULL);

    vfs_mkdir("/dev", 0755);

    // extract cpio
    assert(cpio_extract(&cpio, "/") == EOK);

#ifdef CONFIG_DEVFS_ENABLE
    devfs_init();

    if (vfs_mount(NULL, "devfs", CONFIG_DEVFS_MOUNT, NULL) == NULL) {
        kprintf_warn("Failed to initialize DEVFS!\n");
        for (;;);
    } else {
        kprintf_ok("DEVFS initialized successfully!\n");
    }

    register_std_devices();
    dev_initrd_init(initrd->address);
    dev_e9_init();
    dev_serial_init();
    dev_parallel_init();
    dev_fb_init();
#endif

    // smp_init();
    // limine_parsed_data.smp_enabled = true;

    fs_list("/", -1);

    pci_scan("/etc/pci.ids");
    kprintf_ok("PCI devices parsing done\n");
    if (pcie_init("/etc/pci.ids") != PCIE_STATUS_OK) {
        kprintf_warn("Failed to parse PCIe devices!\n");
    } else {
        kprintf_ok("PCIe devices parsing done\n");
    }

    print_pcie_list();

    fileio_t *f = open("/test2.txt", O_CREATE, 0644);
    assert(f);

    const char temp[8] = "TUTUTUT";
    write(f, temp, sizeof(temp));
    seek(f, 0, SEEK_SET);
    close(f);

    f = open("/test2.txt", 0, 0);
    assert(f);
    char *buf = kmalloc(30);
    assert(buf);
    read(f, 30, buf);
    kprintf("Reading contents: %s\n", buf);

    binfmt_register_loader(&elf_binfmt_loader);

    // ffffffff80045977

    /*
    mod_t *mbr = load_module("/initrd/modules/mbr.km");
    start_module(mbr);

    mod_t *ahci = load_module("/initrd/modules/ahci.km");
    if (!ahci) {
        debugf_warn("Couldn't find AHCI driver!\n");
    }
    start_module(ahci);
    */

    /*
        IDEA:
        create a template VMC for all the processes
        for stuff like scheduler structs, heap structures

        Which means that every component that processes should access must be
       initialized before this function.
    */

    mod_t *ps2 = load_module("/kmod/ps2.km");
    if (!ps2) {
        debugf_warn("Couldn't find PS/2 driver!\n");
    } else {
        start_module(ps2);
    }

    global_vmc_init(kvmc);

    init_cpu_scheduler();

    _disable_interrupts(); // just in case
    irq_registerHandler(0, scheduler_timer_tick);

    vt_init();
    kprintf_ok("Scheduler initialized\n");
    _enable_interrupts();

    for (;;)
        ;
}
