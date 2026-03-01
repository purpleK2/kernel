#include "stdio.h"
#include "dev/tty/vt.h"
#include "interrupts/irq.h"
#include "interrupts/isr.h"

#include <io.h>
#include <limine.h>
#include <stdarg.h>
#include <terminal/terminal.h>
#include <util/va_list.h>

#include <spinlock.h>

#define NANOPRINTF_USE_FIELD_WIDTH_FORMAT_SPECIFIERS     1
#define NANOPRINTF_USE_PRECISION_FORMAT_SPECIFIERS       1
#define NANOPRINTF_USE_FLOAT_FORMAT_SPECIFIERS           0
#define NANOPRINTF_USE_SMALL_FORMAT_SPECIFIERS           1
#define NANOPRINTF_USE_LARGE_FORMAT_SPECIFIERS           1
#define NANOPRINTF_USE_BINARY_FORMAT_SPECIFIERS          1
#define NANOPRINTF_USE_WRITEBACK_FORMAT_SPECIFIERS       0
#define NANOPRINTF_SNPRINTF_SAFE_TRIM_STRING_ON_OVERFLOW 1
typedef long ssize_t;

#define NANOPRINTF_IMPLEMENTATION
#include <nanoprintf.h>

uint32_t current_bg;
uint32_t current_fg;

atomic_flag STDIO_FB_LOCK = ATOMIC_FLAG_INIT;
atomic_flag STDIO_E9_LOCK = ATOMIC_FLAG_INIT;

// yeah, it's a bit brutal :P
void stdio_force_unlock() {
    spinlock_release(&STDIO_FB_LOCK);
    spinlock_release(&STDIO_E9_LOCK);
}

// unlocks spinlocks by force
// interrupts are disabled to avoid other ones to be fired
void stdio_panic_init() {
    asm("cli");

    stdio_force_unlock();
}

uint32_t fb_get_bg() {
    return current_bg;
}

uint32_t fb_get_fg() {
    return current_fg;
}

void fb_set_bg(uint32_t bg_rgb) {
    current_bg = bg_rgb;
    _term_set_bg(bg_rgb);
}

void fb_set_fg(uint32_t fg_rgb) {
    current_fg = fg_rgb;
    _term_set_fg(fg_rgb);
}

void set_screen_bg_fg(uint32_t bg_rgb, uint32_t fg_rgb) {
    fb_set_bg(bg_rgb);
    fb_set_fg(fg_rgb);
}

void clearscreen() {
    _term_cls();
}

void putc(int c, void *ctx) {
    UNUSED(ctx);
    spinlock_acquire(&STDIO_FB_LOCK);
    if (vt_is_console_active()) {
        _term_putc(c);
    }
    spinlock_release(&STDIO_FB_LOCK);
}

void dputc(int c, void *ctx) {
    UNUSED(ctx);
    spinlock_acquire(&STDIO_E9_LOCK);
    _outb(0xE9, c);
    spinlock_release(&STDIO_E9_LOCK);
}

void mputc(int c, void *ctx) {
    putc(c, ctx);
    dputc(c, ctx);
}

extern bool kernel_console_active;

void bsod_init() {
    set_screen_bg_fg(DEFAULT_BG, PANIC_BG);

    kernel_console_active = true;

    clearscreen();
}

int printf(void (*putc_function)(int, void *), const char *fmt, va_list var) {
    return npf_vpprintf(putc_function, NULL, fmt, var);
}

#define KPRINTF_BUF_SIZE 1024

typedef struct {
    char buf[KPRINTF_BUF_SIZE];
    size_t len;
} print_buffer_t;

static print_buffer_t fb_buffer  = { .len = 0 };
static print_buffer_t e9_buffer  = { .len = 0 };

static void flush_buffer(print_buffer_t *buf, void (*putc_fn)(int, void *)) {
    for (size_t i = 0; i < buf->len; i++) {
        putc_fn(buf->buf[i], NULL);
    }
    buf->len = 0;
}

int kprintf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    int written = vsnprintf(fb_buffer.buf + fb_buffer.len,
                            KPRINTF_BUF_SIZE - fb_buffer.len,
                            fmt, args);
    va_end(args);

    if (written > 0) {
        // clip to buffer size
        if ((size_t)written + fb_buffer.len >= KPRINTF_BUF_SIZE) {
            written = KPRINTF_BUF_SIZE - fb_buffer.len;
        }
        fb_buffer.len += written;
    }

	flush_buffer(&fb_buffer, putc);

    return written;
}


int debugf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    int written = vsnprintf(e9_buffer.buf + e9_buffer.len,
                            KPRINTF_BUF_SIZE - e9_buffer.len,
                            fmt, args);
    va_end(args);

    if (written > 0) {
        if ((size_t)written + e9_buffer.len >= KPRINTF_BUF_SIZE) {
            written = KPRINTF_BUF_SIZE - e9_buffer.len;
        }
        e9_buffer.len += written;
    }

    flush_buffer(&e9_buffer, dputc);

    return written;
}


int mprintf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    // write to fb buffer
    int written_fb = vsnprintf(fb_buffer.buf + fb_buffer.len,
                               KPRINTF_BUF_SIZE - fb_buffer.len,
                               fmt, args);
    va_end(args);

    va_start(args, fmt);

    // write to e9 buffer
    int written_e9 = vsnprintf(e9_buffer.buf + e9_buffer.len,
                               KPRINTF_BUF_SIZE - e9_buffer.len,
                               fmt, args);
    va_end(args);

    if (written_fb > 0) {
        if ((size_t)written_fb + fb_buffer.len >= KPRINTF_BUF_SIZE)
            written_fb = KPRINTF_BUF_SIZE - fb_buffer.len;
        fb_buffer.len += written_fb;
    }

    if (written_e9 > 0) {
        if ((size_t)written_e9 + e9_buffer.len >= KPRINTF_BUF_SIZE)
            written_e9 = KPRINTF_BUF_SIZE - e9_buffer.len;
        e9_buffer.len += written_e9;
    }

    flush_buffer(&fb_buffer, putc);
    flush_buffer(&e9_buffer, dputc);

    return written_fb + written_e9;
}

extern uint64_t _get_rbp(void);

int kpanic(const char *cause, ...) {
    va_list args;
    va_start(args, cause);

    uint64_t rbp;
    __asm__ volatile(
        "mov %%rbp, %0"
        : "=r"(rbp)
        :
        :
    );

    set_screen_bg_fg(0x000000, PANIC_FG);
    debugf(ANSI_COLOR_BLUE);

    mprintf("Kernel panic @ 0x%.16llx: ", ((struct stackFrame *)rbp)->rip);

    printf(mputc, cause, args);

    mprintf("\n");

    print_stack_trace(rbp, 0);

    va_end(args);

    _disable_interrupts();
    for (;;)
        __asm__ volatile ("hlt");

    return 0;
}
