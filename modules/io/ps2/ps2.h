#ifndef PS2_KEYBOARD_H
#define PS2_KEYBOARD_H

#include <cpu.h>
#include <dev/meowdev/meowdev.h>
#include <stdint.h>
#include <stdbool.h>

#define PS2_DATA_PORT    0x60
#define PS2_STATUS_PORT  0x64
#define PS2_COMMAND_PORT 0x64

#define PS2_STATUS_OUTPUT_FULL 0x01
#define PS2_STATUS_INPUT_FULL  0x02
#define PS2_STATUS_AUX_DATA    0x20

#define PS2_CMD_READ_CONFIG     0x20
#define PS2_CMD_WRITE_CONFIG    0x60
#define PS2_CMD_ENABLE_AUX_PORT 0xA8
#define PS2_CMD_WRITE_TO_AUX    0xD4

#define PS2_CMD_SET_LEDS 0xED

#define PS2_MOUSE_CMD_RESET          0xFF
#define PS2_MOUSE_CMD_SET_DEFAULTS   0xF6
#define PS2_MOUSE_CMD_ENABLE_REPORTS 0xF4

#define PS2_ACK 0xFA

#define PS2_LED_SCROLL_LOCK 0x01
#define PS2_LED_NUM_LOCK    0x02
#define PS2_LED_CAPS_LOCK   0x04

typedef struct {
    bool shift_left;
    bool shift_right;
    bool ctrl_left;
    bool ctrl_right;
    bool alt_left;
    bool alt_right;
    bool caps_lock;
    bool num_lock;
    bool scroll_lock;
} kb_modifiers_t;

void ps2_keyboard_init(void);

void ps2_keyboard_handler(registers_t *regs);

void ps2_mouse_init(void);

void ps2_mouse_handler(registers_t *regs);

void ps2_set_leds(bool scroll, bool num, bool caps);

extern mdev_device_t *ps2_meowdev_keyboard;
extern mdev_device_t *ps2_meowdev_mouse;

#endif // PS2_KEYBOARD_H