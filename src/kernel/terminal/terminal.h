#ifndef TERMINAL_H
#define TERMINAL_H 1

#include <stdint.h>

struct terminal_ctx {
    uint64_t rows;
    uint64_t columns;
};

extern uint32_t bg_color[3];
extern uint32_t fg_color[3];

void _term_init();
void _term_render_cursor(uint64_t x, uint64_t y);
void _term_move_cursor(uint64_t x, uint64_t y);
void _term_clear_line(int mode);
void _term_clear_screen(int mode);
void _term_save_cursor(void);
void _term_restore_cursor(void);

void _term_putc(char c);
void _term_puts(const char *str);
void _term_cls();

void _term_set_bg(uint32_t rgb);
void _term_set_fg(uint32_t rgb);

#endif // TERMINAL_H