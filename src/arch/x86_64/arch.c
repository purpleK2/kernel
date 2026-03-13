#include "arch.h"

#include <autoconf.h>
#include <cpu.h>
#include <gdt/gdt.h>
#include <idt/idt.h>
#include <interrupts/irq.h>
#include <interrupts/isr.h>
#include <math/fpu.h>
#include <math/sse.h>
#include <paging/paging.h>
#include <pic/pic.h>
#include <pit/pit.h>
#include <tsc/tsc.h>

#include <stdio.h>
#include <time.h>

#define PIT_TICKS (1000 / CONFIG_SCHED_TIMER_INTERVAL_MS)

bool pit;
bool tsc;

void arch_base_init() {
    pit = false;
    tsc = false;

    gdt_init();
    kprintf_ok("Initialized GDT\n");
    idt_init();
    kprintf_ok("Initialized IDT\n");
    isr_init();
    kprintf_ok("Initialized ISRs\n");
    isr_registerHandler(14, pf_handler);

    // _crash_test();

    irq_init();
    kprintf_ok("Initialized IRQs\n");
    pic_config(PIC_REMAP_OFFSET, PIC_REMAP_OFFSET + 8);
    kprintf_ok("Enabled PIC\n");

    irq_registerHandler(0, timer_tick);

    init_fpu();
    kprintf_ok("Initialized FPU\n");
    init_sse();
    kprintf_ok("Initialized SSE1 + SSE2\n");

    pit_init(PIT_TICKS);
    pit = true;
    kprintf_ok("Initialized PIT\n");

    if (check_tsc()) {
        tsc_init();
        tsc = true;
        pit = false;
        kprintf_ok("Initialized TSC\n");
    }
}

void sleep(unsigned long ms) {
    if (!(tsc || pit) || !ms) {
        return;
    }

    if (pit) {
        pit_sleep(ms);
    } else {
        tsc_sleep(ms * 1000); // microseconds = milliseconds * 1000
    }
}

void mark_time(uint64_t *marker) {
    if (!(tsc || pit) || !marker) {
        return;
    }

    if (pit) {
        *marker = get_ticks();
    } else {
        *marker = _get_tsc();
    }
}

uint64_t get_ms(uint64_t start) {
    uint64_t now;

    if (!(tsc || pit) || !start) {
        return 0;
    }

    if (pit) {
        now = get_ticks();

        return (now - start);
    } else {
        now = _get_tsc();

        return ((now - start) * 1000) / tsc_frequency;
    }
}