#include "terminal.h"
#include "stdio.h"

#include <graphical/framebuffer.h>
#include <limine.h>
#include <terminal/psf.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MAX_ROWS 256
#define MAX_COLS 256

uint64_t char_cursor_x = 0;
uint64_t char_cursor_y = 0;
static uint64_t old_cursor_x = 0;
static uint64_t old_cursor_y = 0;

struct terminal_ctx terminal_ctx;
struct limine_framebuffer *framebuffer_86 = NULL;
bool init                                 = false;

uint32_t bg_color[3] = {0x00, 0x00, 0x00};
uint32_t fg_color[3] = {0xFF, 0xFF, 0xFF};

// TODO: probably a struct is better
static char screen_buffer[MAX_ROWS][MAX_COLS];
static uint32_t rgb_buffer[MAX_ROWS][MAX_COLS];

static uint64_t scroll_base = 0;

static void draw_char_at(uint64_t x, uint64_t y, char c, uint32_t r, uint32_t g,
                         uint32_t b) {
    uint64_t px = x * 8;
    uint64_t py = y * 14;
    psfPutC(c, px, py, r, g, b);
}

void _term_set_bg(uint32_t rgb) {
    if (!init)
        _term_init();

    bg_color[0] = (rgb >> framebuffer_86->red_mask_shift) & 0xFF;
    bg_color[1] = (rgb >> framebuffer_86->green_mask_shift) & 0xFF;
    bg_color[2] = (rgb >> framebuffer_86->blue_mask_shift) & 0xFF;
}

void _term_set_fg(uint32_t rgb) {
    if (!init)
        _term_init();

    fg_color[0] = (rgb >> framebuffer_86->red_mask_shift) & 0xFF;
    fg_color[1] = (rgb >> framebuffer_86->green_mask_shift) & 0xFF;
    fg_color[2] = (rgb >> framebuffer_86->blue_mask_shift) & 0xFF;
}

static void render_screen() {
    for (uint64_t y = 0; y < terminal_ctx.rows; y++) {
        uint64_t real_row = (scroll_base + y) % MAX_ROWS;
        for (uint64_t x = 0; x < terminal_ctx.columns; x++) {
            char c       = screen_buffer[real_row][x];
            uint32_t rgb = rgb_buffer[real_row][x];

            uint32_t r = (rgb >> framebuffer_86->red_mask_shift) & 0xFF;
            uint32_t g = (rgb >> framebuffer_86->green_mask_shift) & 0xFF;
            uint32_t b = (rgb >> framebuffer_86->blue_mask_shift) & 0xFF;

            draw_char_at(x, y, c, r, g, b);
        }
    }
}

void _term_init() {
    framebuffer_86 = get_bootloader_data()->framebuffer;
    if (!framebuffer_86 || !framebuffer_86->address)
        return;

    terminal_ctx.rows    = framebuffer_86->height / 14;
    terminal_ctx.columns = framebuffer_86->width / 8;

    _term_cls();
    render_screen();
    init = true;
}

void _term_cls() {
    if (!framebuffer_86 || !framebuffer_86->address)
        return;

    for (uint64_t y = 0; y < framebuffer_86->height; y++) {
        for (uint64_t x = 0; x < framebuffer_86->width; x++) {
            drawPixel(x, y, bg_color[0], bg_color[1], bg_color[2]);
        }
    }

    for (uint64_t i = 0; i < MAX_ROWS; i++) {
        for (uint64_t j = 0; j < MAX_COLS; j++) {
            screen_buffer[i][j] = ' ';
        }
    }

    char_cursor_x = 0;
    char_cursor_y = 0;
    scroll_base   = 0;
    render_screen();
    _term_render_cursor(char_cursor_x, char_cursor_y);
}

void _term_render_cursor(uint64_t x, uint64_t y) {
    if (!init || !framebuffer_86)
        return;

    if (old_cursor_x != x || old_cursor_y != y) {
        uint64_t old_row = (scroll_base + old_cursor_y) % MAX_ROWS;
        char old_char = screen_buffer[old_row][old_cursor_x];
        uint32_t old_rgb = rgb_buffer[old_row][old_cursor_x];
        uint32_t r = (old_rgb >> framebuffer_86->red_mask_shift) & 0xFF;
        uint32_t g = (old_rgb >> framebuffer_86->green_mask_shift) & 0xFF;
        uint32_t b = (old_rgb >> framebuffer_86->blue_mask_shift) & 0xFF;
        if (old_char == 0) old_char = ' ';
        draw_char_at(old_cursor_x, old_cursor_y, old_char, r, g, b);
    }

    uint64_t px = x * 8;
    uint64_t py = y * 14;

    for (uint64_t i = 0; i < 8; i++) {
        for (uint64_t j = 0; j < 14; j++) {
            drawPixel(px + i, py + j, fg_color[0], fg_color[1], fg_color[2]);
        }
    }

    old_cursor_x = x;
    old_cursor_y = y;
}

static void scroll_up_by_one() {
    scroll_base = (scroll_base + 1) % MAX_ROWS;

    uint64_t new_line = (scroll_base + terminal_ctx.rows - 1) % MAX_ROWS;
    for (uint64_t i = 0; i < MAX_COLS; i++) {
        screen_buffer[new_line][i] = ' ';
    }

    render_screen();
}

void _term_putc(char c) {
    if (!init)
        _term_init();

    if (c == '\n') {
        char_cursor_x = 0;
        char_cursor_y++;
    } else if (c == '\r') {
        char_cursor_x = 0;
    } else if (c == '\b') {
        if (char_cursor_x > 0) {
            char_cursor_x--;
            uint64_t row = (scroll_base + char_cursor_y) % MAX_ROWS;
            screen_buffer[row][char_cursor_x] = ' ';
            draw_char_at(char_cursor_x, char_cursor_y, ' ', fg_color[0],
                         fg_color[1], fg_color[2]);
        }
    } else if (c == '\t') {
        for (int i = 0; i < 4; i++) {
            _term_putc(' ');
        }
    } else {
        uint64_t row = (scroll_base + char_cursor_y) % MAX_ROWS;
        screen_buffer[row][char_cursor_x] = c;

        uint32_t color = (fg_color[0] << framebuffer_86->red_mask_shift) |
                         (fg_color[1] << framebuffer_86->green_mask_shift) |
                         (fg_color[2] << framebuffer_86->blue_mask_shift);
        rgb_buffer[row][char_cursor_x] = color;

        draw_char_at(char_cursor_x, char_cursor_y, c, fg_color[0], fg_color[1],
                     fg_color[2]);
        char_cursor_x++;
    }

    if (char_cursor_x >= terminal_ctx.columns) {
        char_cursor_x = 0;
        char_cursor_y++;
    }

    if (char_cursor_y >= terminal_ctx.rows) {
        char_cursor_y = terminal_ctx.rows - 1;
        scroll_up_by_one();
    }

    _term_render_cursor(char_cursor_x, char_cursor_y);
}

void _term_puts(const char *str) {
    if (!init)
        _term_init();
    while (*str) {
        _term_putc(*str++);
    }
}
