#include "uacpi/kernel_api.h"

#include <arch.h>
#include <cpu.h>
#include <interrupts/irq.h>
#include <interrupts/isr.h>
#include <io.h>
#include <kernel.h>

#include <memory/heap/kheap.h>
#include <memory/vmm/vflags.h>
#include <memory/vmm/vmm.h>
#include <paging/paging.h>

#include <acpi/uacpi/types.h>
#include <acpi_arch_helpers.h>

#include <pci/pci.h>

#include <semaphore.h>
#include <spinlock.h>

#include <uacpi/status.h>

#include <util/assert.h>
#include <util/util.h>

#include <stdio.h>
#include <string.h>
#include <time.h>

// --- fun fact: `uacpi_phys_addr` is uint64_t btw ---

uacpi_status uacpi_kernel_get_rsdp(uacpi_phys_addr *out_rsdp_address) {
    bootloader_data *limine_data = get_bootloader_data();
    if (!limine_data)
        return UACPI_STATUS_NOT_FOUND;

    if (!limine_data->rsdp_table_address)
        return UACPI_STATUS_NOT_FOUND;

    uint64_t rsdp_phys = VIRT_TO_PHYSICAL(limine_data->rsdp_table_address);
    *out_rsdp_address  = rsdp_phys;

    return UACPI_STATUS_OK;
}

void *uacpi_kernel_map(uacpi_phys_addr addr, uacpi_size len) {
    uint64_t aligned = ROUND_DOWN(addr, PFRAME_SIZE);
    size_t offset    = (size_t)(addr - aligned);

    size_t actual_len = len + offset; // this is BYTES!!!
    size_t pages      = ROUND_UP(actual_len, PFRAME_SIZE) / PFRAME_SIZE;

    void *virt = valloc(get_kernel_vmc(), pages, VMO_KERNEL_RW | VMO_NX,
                        (void *)aligned);

    // re-align the pointer to original addr offset

    virt += offset;

    return virt;
}

void uacpi_kernel_unmap(void *addr, uacpi_size len) {
    uint64_t aligned = ROUND_DOWN((uint64_t)addr, PFRAME_SIZE);
    UNUSED(len);

    vfree(get_kernel_vmc(), (void *)aligned, false);
}

#ifndef UACPI_FORMATTED_LOGGING
void uacpi_kernel_log(uacpi_log_level log_level, const uacpi_char *fmt) {
    uint32_t prev_bg = fb_get_bg();
    uint32_t prev_fg = fb_get_fg();

    switch (log_level) {
    case UACPI_LOG_DEBUG:
        debugf(ANSI_COLOR_GRAY "[%llu.%03llu] (uacpi) %s", get_ticks() / 1000,
               get_ticks() % 1000, fmt);
        break;
    case UACPI_LOG_TRACE:
        debugf(ANSI_COLOR_GRAY "[%llu.%03llu] (uacpi) %s", get_ticks() / 1000,
               get_ticks() % 1000, fmt);
        break;

    case UACPI_LOG_INFO:
        debugf(ANSI_COLOR_GRAY "[%llu.%03llu] (uacpi) %s", get_ticks() / 1000,
               get_ticks() % 1000, fmt);
        fb_set_fg(INFO_FG);
        kprintf("[%llu.%03llu] (uacpi) %s", get_ticks() / 1000,
                get_ticks() % 1000, fmt);
        break;
    case UACPI_LOG_WARN:
        fb_set_fg(WARNING_FG);
        debugf(ANSI_COLOR_ORANGE "[%llu.%03llu] (uacpi) %s", get_ticks() / 1000,
               get_ticks() % 1000, fmt);
        kprintf("[%llu.%03llu] (uacpi) %s", get_ticks() / 1000,
                get_ticks() % 1000, fmt);
        break;
    case UACPI_LOG_ERROR:
        fb_set_fg(PANIC_FG);
        debugf(ANSI_COLOR_RED "[%llu.%03llu] (uacpi) %s", get_ticks() / 1000,
               get_ticks() % 1000, fmt);
        kprintf("[%llu.%03llu] (uacpi) %s", get_ticks() / 1000,
                get_ticks() % 1000, fmt);

    default:
        break;
    }

    debugf(ANSI_COLOR_RESET);
    set_screen_bg_fg(prev_bg, prev_fg);
}
#else
void uacpi_kernel_log(uacpi_log_level log_level, const uacpi_char *fmt, ...) {
    char buffer[1024];
    va_list args;

    va_start(args, fmt);
    int length = npf_vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    if (length < 0 || length >= (int)sizeof(buffer)) {
        return;
    }

    uint32_t prev_bg = fb_get_bg();
    uint32_t prev_fg = fb_get_fg();

    switch (log_level) {
    case UACPI_LOG_DEBUG:
        debugf(ANSI_COLOR_GRAY "[%llu.%03llu] (uacpi) %s", get_ticks() / 1000,
               get_ticks() % 1000, buffer);
        break;
    case UACPI_LOG_TRACE:
        debugf(ANSI_COLOR_GRAY "[%llu.%03llu] (uacpi) %s", get_ticks() / 1000,
               get_ticks() % 1000, buffer);
        break;

    case UACPI_LOG_INFO:
        debugf(ANSI_COLOR_GRAY "[%llu.%03llu] (uacpi) %s", get_ticks() / 1000,
               get_ticks() % 1000, buffer);
        fb_set_fg(INFO_FG);
        kprintf("[%llu.%03llu] (uacpi) %s", get_ticks() / 1000,
                get_ticks() % 1000, buffer);
        break;
    case UACPI_LOG_WARN:
        fb_set_fg(WARNING_FG);
        debugf(ANSI_COLOR_ORANGE "[%llu.%03llu] (uacpi) %s", get_ticks() / 1000,
               get_ticks() % 1000, buffer);
        kprintf("[%llu.%03llu] (uacpi) %s", get_ticks() / 1000,
                get_ticks() % 1000, buffer);
        break;
    case UACPI_LOG_ERROR:
        fb_set_fg(PANIC_FG);
        debugf(ANSI_COLOR_RED "[%llu.%03llu] (uacpi) %s", get_ticks() / 1000,
               get_ticks() % 1000, buffer);
        kprintf("[%llu.%03llu] (uacpi) %s", get_ticks() / 1000,
                get_ticks() % 1000, buffer);

    default:
        break;
    }

    debugf(ANSI_COLOR_RESET);
    set_screen_bg_fg(prev_bg, prev_fg);
}

void uacpi_kernel_vlog(uacpi_log_level log_level, const uacpi_char *fmt,
                       uacpi_va_list va_args) {

    char buffer[1024];

    int length = npf_vsnprintf(buffer, sizeof(buffer), fmt, va_args);

    if (length < 0 || length >= (int)sizeof(buffer)) {
        return;
    }

    uacpi_kernel_log(log_level, "%s", buffer);
}
#endif

uacpi_interrupt_state uacpi_kernel_disable_interrupts(void) {
    uacpi_interrupt_state state = (uacpi_interrupt_state)_get_cpu_flags();
    _disable_interrupts();
    return state;
}

void uacpi_kernel_restore_interrupts(uacpi_interrupt_state state) {
    _set_cpu_flags((uint64_t)state);
}

#ifndef UACPI_BAREBONES_MODE

#ifdef UACPI_KERNEL_INITIALIZATION
// note for future Omar: other stuff calls this function to say:
// "Hey bro we need to init UACPI with this init level"
// and you initialize the proper components for that init level
uacpi_status uacpi_kernel_initialize(uacpi_init_level current_init_lvl) {
    switch (current_init_lvl) {
    case UACPI_INIT_LEVEL_EARLY:
        break;

    default:
        return UACPI_STATUS_DENIED;
    }

    return UACPI_STATUS_OK;
}

void uacpi_kernel_deinitialize(void) {
    return;
}
#endif

uacpi_status uacpi_kernel_pci_device_open(uacpi_pci_address address,
                                          uacpi_handle *out_handle) {
}

void uacpi_kernel_pci_device_close(uacpi_handle handle) {
    assert(handle);

    handle = NULL;
}

uacpi_status uacpi_kernel_pci_read8(uacpi_handle device, uacpi_size offset,
                                    uacpi_u8 *value) {
    return UACPI_STATUS_UNIMPLEMENTED;
}
uacpi_status uacpi_kernel_pci_read16(uacpi_handle device, uacpi_size offset,
                                     uacpi_u16 *value) {
    return UACPI_STATUS_UNIMPLEMENTED;
}
uacpi_status uacpi_kernel_pci_read32(uacpi_handle device, uacpi_size offset,
                                     uacpi_u32 *value) {
    return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_pci_write8(uacpi_handle device, uacpi_size offset,
                                     uacpi_u8 value) {
    return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_pci_write16(uacpi_handle device, uacpi_size offset,
                                      uacpi_u16 value) {
    return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_pci_write32(uacpi_handle device, uacpi_size offset,
                                      uacpi_u32 value) {
    return UACPI_STATUS_UNIMPLEMENTED;
}

typedef struct {
    uint16_t base;
    uint16_t max_offset;
} io_t;

uacpi_status uacpi_kernel_io_map(uacpi_io_addr base, uacpi_size len,
                                 uacpi_handle *out_handle) {

    io_t *io = kmalloc(sizeof(io_t));
    if (!io)
        return UACPI_STATUS_OUT_OF_MEMORY;

    io->base       = (uint16_t)base;
    io->max_offset = (uint16_t)len;

    ((io_t **)out_handle)[0] = io;

    return UACPI_STATUS_OK;
}

void uacpi_kernel_io_unmap(uacpi_handle handle) {

    io_t *to_free = ((io_t **)handle)[0];
    if (!to_free)
        return;

    kfree(to_free);

    ((io_t **)handle)[0] = NULL;
}

uacpi_status uacpi_kernel_io_read8(uacpi_handle range, uacpi_size offset,
                                   uacpi_u8 *out_value) {
    io_t *io = range;
    if (!range)
        return UACPI_STATUS_NOT_FOUND;

    uacpi_u16 port = (uacpi_u16)io->base + (uacpi_u16)offset;
    out_value[0]   = _inb(port);

    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_read16(uacpi_handle range, uacpi_size offset,
                                    uacpi_u16 *out_value) {
    io_t *io = range;
    if (!range)
        return UACPI_STATUS_NOT_FOUND;

    uacpi_u16 port = (uacpi_u16)io->base + (uacpi_u16)offset;
    out_value[0]   = _inw(port);

    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_read32(uacpi_handle range, uacpi_size offset,
                                    uacpi_u32 *out_value) {
    io_t *io = range;
    if (!io)
        return UACPI_STATUS_NOT_FOUND;

    uacpi_u16 port = (uacpi_u16)io->base + (uacpi_u16)offset;
    out_value[0]   = _ind(port);

    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write8(uacpi_handle range, uacpi_size offset,
                                    uacpi_u8 in_value) {
    io_t *io = range;
    if (!io)
        return UACPI_STATUS_NOT_FOUND;

    uacpi_u16 port = (uacpi_u16)io->base + (uacpi_u16)offset;
    _outb(port, in_value);

    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write16(uacpi_handle range, uacpi_size offset,
                                     uacpi_u16 in_value) {
    io_t *io = range;
    if (!io)
        return UACPI_STATUS_NOT_FOUND;

    uacpi_u16 port = (uacpi_u16)io->base + (uacpi_u16)offset;
    _outw(port, in_value);

    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write32(uacpi_handle range, uacpi_size offset,
                                     uacpi_u32 in_value) {
    io_t *io = range;
    if (!io)
        return UACPI_STATUS_NOT_FOUND;

    uacpi_u16 port = (uacpi_u16)io->base + (uacpi_u16)offset;
    _outd(port, in_value);

    return UACPI_STATUS_OK;
}

void *uacpi_kernel_alloc(uacpi_size size) {
    return kmalloc(size);
}

#ifdef UACPI_NATIVE_ALLOC_ZEROED
void *uacpi_kernel_alloc_zeroed(uacpi_size size) {
    void *base = kmalloc(size);
    memset(base, 0, size);
}
#endif

#ifndef UACPI_SIZED_FREES
void uacpi_kernel_free(void *mem) {
    if (!mem)
        return;

    kfree(mem);
}
#else
void uacpi_kernel_free(void *mem, uacpi_size size_hint) {
    UNUSED(size_hint);

    if (!mem)
        return;

    kfree(mem);
}
#endif

uacpi_u64 uacpi_kernel_get_nanoseconds_since_boot(void) {
    bootloader_data *limine_data = get_bootloader_data();

    uacpi_u64 boot_ns  = limine_data->boot_time;
    boot_ns           *= 1000; // 1 ms = 1000 ns

    return boot_ns;
}

void uacpi_kernel_stall(uacpi_u8 usec) {
    for (uacpi_u8 i = 0; i < usec; i++)
        ;
}

void uacpi_kernel_sleep(uacpi_u64 msec) {
    sleep(msec);
}

uacpi_handle uacpi_kernel_create_mutex(void) {
    atomic_flag *atomic = kmalloc(sizeof(atomic_flag));

    atomic_flag ugh = ATOMIC_FLAG_INIT;
    *atomic         = ugh;

    return atomic;
}

void uacpi_kernel_free_mutex(uacpi_handle atomic) {
    if (!atomic)
        return;

    kfree(atomic);
}

uacpi_handle uacpi_kernel_create_event(void) {
    semaphore_t *semaphore = kmalloc(sizeof(semaphore_t));

    return (uacpi_handle)semaphore;
}
void uacpi_kernel_free_event(uacpi_handle semaphore) {
    if (!semaphore)
        return;

    kfree(semaphore);
}

uacpi_thread_id uacpi_kernel_get_thread_id(void) {
    return NULL;
}

uacpi_status uacpi_kernel_acquire_mutex(uacpi_handle spinlock,
                                        uacpi_u16 timeout) {

    switch (timeout) {
    case 0x0:
        if (!atomic_flag_test_and_set(spinlock))
            return UACPI_STATUS_TIMEOUT;

        break;

    case 0x0001 ... 0xFFFE: {
        uint64_t t = timeout;
        while (atomic_flag_test_and_set(spinlock)) {
            if (--t == 0) {
                // Handle potential deadlock
                // Options: panic, log, or return failure
                uacpi_kernel_log(UACPI_LOG_WARN,
                                 "Spinlock deadlock detected\n");
                return UACPI_STATUS_TIMEOUT;
            }

            // Optional: small delay to reduce CPU thrashing
            asm("pause");
        }
        break;
    }
    case 0xFFFF:
        spinlock_acquire(spinlock);
        break;

    default:
        return UACPI_STATUS_INVALID_ARGUMENT;
    }

    return UACPI_STATUS_OK;
}

void uacpi_kernel_release_mutex(uacpi_handle spinlock) {
    spinlock_release(spinlock);
}

uacpi_bool uacpi_kernel_wait_for_event(uacpi_handle semaphore,
                                       uacpi_u16 timeout) {

    semaphore_t sem = *(semaphore_t *)semaphore;

    switch (timeout) {
    case 0xFFFF:
        for (;;) {
            if (sem > 0) {
                sem                       -= 1;
                *(semaphore_t *)semaphore  = sem;
                return UACPI_TRUE;
            }
            sleep(1);
        }
        break;

    default:
        while (--timeout > 0) {
            if (sem > 0) {
                sem                       -= 1;
                *(semaphore_t *)semaphore  = sem;
                return UACPI_TRUE;
            }
            sleep(1);
        }
        break;
    }

    return UACPI_FALSE;
}

void uacpi_kernel_signal_event(uacpi_handle semaphore) {
    *(semaphore_t *)semaphore += 1;
}

void uacpi_kernel_reset_event(uacpi_handle semaphore) {
    *(semaphore_t *)semaphore = 0;
}

uacpi_status
uacpi_kernel_handle_firmware_request(uacpi_firmware_request *fw_req) {
    switch (fw_req->type) {
    case UACPI_FIRMWARE_REQUEST_TYPE_BREAKPOINT:
        uacpi_kernel_log(UACPI_LOG_INFO,
                         "Uhmmm everything's OK, just a silly breakpoint :3\n");
        break;

    case UACPI_FIRMWARE_REQUEST_TYPE_FATAL:
        uacpi_kernel_log(UACPI_LOG_ERROR,
                         "Oh no fatal exception happened: \n\ttype: %hhu "
                         "\n\tcode: %u \n\t arg: %lu",
                         fw_req->fatal.type, fw_req->fatal.code,
                         fw_req->fatal.arg);
        return UACPI_STATUS_INTERNAL_ERROR;

    default:
        break;
    }

    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_install_interrupt_handler(
    uacpi_u32 irq, uacpi_interrupt_handler handler, uacpi_handle ctx,
    uacpi_handle *out_irq_handle) {
    if (!handler) {
        UACPI_STATUS_NO_HANDLER;
    }

    uacpi_register_handler(irq, ctx, handler);

    uint32_t *irq_handle = kmalloc(sizeof(uint32_t));
    *irq_handle          = irq;

    *(uint32_t **)out_irq_handle = irq_handle;

    return UACPI_STATUS_OK;
}

uacpi_status
uacpi_kernel_uninstall_interrupt_handler(uacpi_interrupt_handler handler,
                                         uacpi_handle irq_handle) {
    UNUSED(handler);

    uint32_t *irq = irq_handle;
    uacpi_unregister_handler(irq[0]);

    return UACPI_STATUS_OK;
}

uacpi_handle uacpi_kernel_create_spinlock(void) {
    atomic_flag *atomic = kmalloc(sizeof(atomic_flag));

    return atomic;
}

void uacpi_kernel_free_spinlock(uacpi_handle atomic) {
    if (!atomic)
        return;

    kfree(atomic);
}

uacpi_cpu_flags uacpi_kernel_lock_spinlock(uacpi_handle lock) {
    uacpi_cpu_flags flags_before = (uacpi_cpu_flags)_get_cpu_flags();

    spinlock_acquire(lock);

    return flags_before;
}

void uacpi_kernel_unlock_spinlock(uacpi_handle lock,
                                  uacpi_cpu_flags flags_to_set) {
    spinlock_release(lock);

    _set_cpu_flags(flags_to_set);
}

uacpi_status uacpi_kernel_schedule_work(uacpi_work_type work_type,
                                        uacpi_work_handler work_handler,
                                        uacpi_handle ctx) {
    UNUSED(work_type);
    UNUSED(work_handler);
    UNUSED(ctx);

    return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_wait_for_work_completion(void) {
    return UACPI_STATUS_UNIMPLEMENTED;
}

#endif // ifndef UACPI_BAREBONES_MODE
