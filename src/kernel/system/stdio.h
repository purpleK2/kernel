#ifndef STDIO_H
#define STDIO_H 1

// #define PRINTF_MIRROR            // printf() will be mirrored to the E9 port
#define BETTER_DEBUG // better debug info (with function names and line)

#include <nanoprintf.h>
#include <tsc/tsc.h>

#include <stdint.h>

#include <time.h>

extern bool pit;

#define DEFAULT_FG 0xeeeeee
#define DEFAULT_BG 0x050505

#define INFO_FG    0xa4a5a4
#define SUCCESS_FG 0x00e826
#define WARNING_FG 0xea7500

#define PANIC_FG   DEFAULT_FG
#define PANIC_BG 0x6b0160

uint32_t fb_get_bg();
void fb_set_bg(uint32_t bg_rgb);

uint32_t fb_get_fg();
void fb_set_fg(uint32_t fg_rgb);

void set_screen_bg_fg(uint32_t bg_rgb, uint32_t fg_rgb);

void stdio_force_unlock();
void stdio_panic_init();
void clearscreen();
void bsod_init();

void putc(int c, void *ctx);
void dputc(int c, void *ctx);
void mputc(int c, void *ctx);

int printf(void (*putc_function)(int, void *), const char *fmt, va_list var);

int kprintf(const char *fmt, ...);
int debugf(const char *fmt, ...);
int mprintf(const char *fmt, ...);

int kpanic(const char *cause, ...);

// snprintf and such are copied from nanoprintf directly

#define sprintf(buf, fmt, ...)                                                 \
    npf_snprintf(buf, sizeof(buf), fmt, ##__VA_ARGS__)
#define vsnprintf(buf, len, fmt, ...)                                          \
    npf_vsnprintf(buf, len, fmt, ##__VA_ARGS__)
#define snprintf(buf, len, fmt, ...) npf_snprintf(buf, len, fmt, ##__VA_ARGS__)

/*#define kprintf_ok(fmt, ...) \
    ({                                                                         \
        uint32_t prev_fg = fb_get_fg();                                        \
        fb_set_fg(0x00e826);                                                   \
        kprintf("[ %s():%d::SUCCESS ] " fmt, __FUNCTION__, __LINE__,           \
                ##__VA_ARGS__);                                                \
        fb_set_fg(prev_fg);                                                    \
    })

#define kprintf_info(fmt, ...)                                                 \
    ({                                                                         \
        uint32_t prev_fg = fb_get_fg();                                        \
        fb_set_fg(INFO_FG);                                                    \
        kprintf("[ %s():%d::INFO ] " fmt, __FUNCTION__, __LINE__,              \
                ##__VA_ARGS__);                                                \
        fb_set_fg(prev_fg);                                                    \
    })

#define kprintf_warn(fmt, ...)                                                 \
    ({                                                                         \
        uint32_t prev_fg = fb_get_fg();                                        \
        fb_set_fg(WARNING_FG);                                                 \
        kprintf("--- [ WARNING @ %s():%u ] --- " fmt, __FUNCTION__, __LINE__,  \
                ##__VA_ARGS__);                                                \
        fb_set_fg(prev_fg);                                                    \
    })

#define kprintf_panic(fmt, ...)                                                \
    ({                                                                         \
        uint32_t prev_fg = fb_get_fg();                                        \
        fb_set_fg(PANIC_FG);                                                   \
        kprintf("--- [ PANIC @ %s():%d ] --- " fmt " Halting...",              \
                __FUNCTION__, __LINE__, ##__VA_ARGS__);                        \
        fb_set_fg(prev_fg);                                                    \
    })*/

#define kprintf_ok(fmt, ...)                                                   \
    ({                                                                         \
        uint32_t prev_fg = fb_get_fg();                                        \
        fb_set_fg(SUCCESS_FG);                                                 \
        if (!(pit || tsc)) {                                                   \
            kprintf("early: (%s:%d) " fmt, __FUNCTION__, __LINE__,             \
                    ##__VA_ARGS__);                                            \
        } else {                                                               \
            kprintf("[%llu.%03llu] (%s:%d) " fmt, get_ticks() / 1000,          \
                    get_ticks() % 1000, __FUNCTION__, __LINE__,                \
                    ##__VA_ARGS__);                                            \
        }                                                                      \
        fb_set_fg(prev_fg);                                                    \
    })

#define kprintf_info(fmt, ...)                                                 \
    ({                                                                         \
        uint32_t prev_fg = fb_get_fg();                                        \
        fb_set_fg(INFO_FG);                                                    \
        if (!(pit || tsc)) {                                                   \
            kprintf("early: (%s:%d) " fmt, __FUNCTION__, __LINE__,             \
                    ##__VA_ARGS__);                                            \
        } else {                                                               \
            kprintf("[%llu.%03llu] (%s:%d) " fmt, get_ticks() / 1000,          \
                    get_ticks() % 1000, __FUNCTION__, __LINE__,                \
                    ##__VA_ARGS__);                                            \
        }                                                                      \
        fb_set_fg(prev_fg);                                                    \
    })

#define kprintf_warn(fmt, ...)                                                 \
    ({                                                                         \
        uint32_t prev_fg = fb_get_fg();                                        \
        fb_set_fg(WARNING_FG);                                                 \
        if (!(pit || tsc)) {                                                   \
            kprintf("early: (%s:%d) " fmt, __FUNCTION__, __LINE__,             \
                    ##__VA_ARGS__);                                            \
        } else {                                                               \
            kprintf("[%llu.%03llu] (%s:%d) " fmt, get_ticks() / 1000,          \
                    get_ticks() % 1000, __FUNCTION__, __LINE__,                \
                    ##__VA_ARGS__);                                            \
        }                                                                      \
        fb_set_fg(prev_fg);                                                    \
    })

#define kprintf_panic(fmt, ...)                                                \
    ({                                                                         \
        uint32_t prev_fg = fb_get_fg();                                        \
        fb_set_fg(PANIC_FG);                                                   \
        if (!(pit || tsc)) {                                                   \
            kprintf("early: (%s:%d) " fmt " Halting...", __FUNCTION__,         \
                    __LINE__, ##__VA_ARGS__);                                  \
        } else {                                                               \
            kprintf("[%llu.%03llu] (%s:%d) " fmt " Halting...",                \
                    get_ticks() / 1000, get_ticks() % 1000, __FUNCTION__,      \
                    __LINE__, ##__VA_ARGS__);                                  \
        }                                                                      \
        fb_set_fg(prev_fg);                                                    \
    })

#define ANSI_COLOR_RED    "\33[31m"
#define ANSI_COLOR_GREEN  "\33[32m"
#define ANSI_COLOR_ORANGE "\33[33m"
#define ANSI_COLOR_GRAY   "\33[90m"
#define ANSI_COLOR_BLUE   "\x1b[38;2;102;163;255m"
#define ANSI_COLOR_RESET  "\33[0m"

#define COLOR(color, str) color str ANSI_COLOR_RESET

/*#define debugf_debug(fmt, ...) \
    debugf(COLOR(ANSI_COLOR_GRAY, "[ %s()::DEBUG ] " fmt), __FUNCTION__,       \
           ##__VA_ARGS__)
#define debugf_ok(fmt, ...)                                                    \
    debugf(COLOR(ANSI_COLOR_GREEN, "[ %s()::SUCCESS ] " fmt), __FUNCTION__,    \
           ##__VA_ARGS__)

#define debugf_warn(fmt, ...)                                                  \
    debugf(COLOR(ANSI_COLOR_ORANGE, "--- [ WARNING @ %s():%u ] --- " fmt),     \
           __FUNCTION__, __LINE__, ##__VA_ARGS__);

#define debugf_panic(fmt, ...)                                                 \
    debugf(COLOR(ANSI_COLOR_RED, "[ %s()::PANIC ] " fmt), __FUNCTION__,        \
           ##__VA_ARGS__)*/

#define debugf_debug(fmt, ...)                                                 \
    ({                                                                         \
        if (!(pit || tsc)) {                                                   \
            debugf(COLOR(ANSI_COLOR_GRAY, "early: (%s:%d) " fmt),              \
                   __FUNCTION__, __LINE__, ##__VA_ARGS__);                     \
        } else {                                                               \
            debugf(COLOR(ANSI_COLOR_GRAY, "[%llu.%03llu] (%s:%d) " fmt),       \
                   get_ticks() / 1000, get_ticks() % 1000, __FUNCTION__,       \
                   __LINE__, ##__VA_ARGS__);                                   \
        }                                                                      \
    })

#define debugf_ok(fmt, ...)                                                    \
    ({                                                                         \
        if (!(pit || tsc)) {                                                   \
            debugf(COLOR(ANSI_COLOR_GREEN, "early: (%s:%d) " fmt),             \
                   __FUNCTION__, __LINE__, ##__VA_ARGS__);                     \
        } else {                                                               \
            debugf(COLOR(ANSI_COLOR_GREEN, "[%llu.%03llu] (%s:%d) " fmt),      \
                   get_ticks() / 1000, get_ticks() % 1000, __FUNCTION__,       \
                   __LINE__, ##__VA_ARGS__);                                   \
        }                                                                      \
    })

#define debugf_warn(fmt, ...)                                                  \
    ({                                                                         \
        if (!(pit || tsc)) {                                                   \
            debugf(COLOR(ANSI_COLOR_ORANGE, "early: (%s:%d) " fmt),            \
                   __FUNCTION__, __LINE__, ##__VA_ARGS__);                     \
        } else {                                                               \
            debugf(COLOR(ANSI_COLOR_ORANGE, "[%llu.%03llu] (%s:%d) " fmt),     \
                   get_ticks() / 1000, get_ticks() % 1000, __FUNCTION__,       \
                   __LINE__, ##__VA_ARGS__);                                   \
        }                                                                      \
    })

#define debugf_panic(fmt, ...)                                                 \
    ({                                                                         \
        if (!(pit || tsc)) {                                                   \
            debugf(COLOR(ANSI_COLOR_RED, "early: (%s:%d) " fmt), __FUNCTION__, \
                   __LINE__, ##__VA_ARGS__);                                   \
        } else {                                                               \
            debugf(COLOR(ANSI_COLOR_RED, "[%llu.%03llu] (%s:%d) " fmt),        \
                   get_ticks() / 1000, get_ticks() % 1000, __FUNCTION__,       \
                   __LINE__, ##__VA_ARGS__);                                   \
        }                                                                      \
    })

#define mprintf_info(fmt, ...)                                                 \
    ({                                                                         \
        kprintf_info(fmt, ##__VA_ARGS__);                                      \
        debugf_debug(FMT, ##__VA_ARGS__);                                      \
    })

#define mprintf_ok(fmt, ...)                                                   \
    ({                                                                         \
        kprintf_ok(fmt, ##__VA_ARGS__);                                        \
        debugf_ok(FMT, ##__VA_ARGS__);                                         \
    })

#define mprintf_warn(fmt, ...)                                                 \
    ({                                                                         \
        kprintf_warn(fmt, ##__VA_ARGS__);                                      \
        debugf_warn(fmt, ##__VA_ARGS__);                                       \
    })

#define mprintf_panic(fmt, ...)                                                \
    ({                                                                         \
        kprintf_panic(fmt, ##__VA_ARGS__);                                     \
        debugf_panic(fmt, ##__VA_ARGS__);                                      \
    })

#endif
