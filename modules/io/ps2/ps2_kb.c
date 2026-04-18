#include "ps2.h"

#include "dev/tty/tty.h"
#include "dev/tty/vt.h"
#include "interrupts/irq.h"
#include "io.h"
#include "stdio.h"

#include <string.h>

static kb_modifiers_t modifiers = {0};
static bool extended            = false;
static bool key_down[512]       = {0};

// US QWERTY scancode to ASCII table (scancode set 1)
static const char scancode_to_ascii[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0,
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z',
    'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ',
    0,                            // Right shift,
                                  // Keypad *, Left
                                  // Alt, Space, Caps
                                  // Lock
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // F1-F10
    0, 0,                         // Num Lock, Scroll Lock
    '7', '8', '9', '-',           // Keypad
    '4', '5', '6', '+',           // Keypad
    '1', '2', '3',                // Keypad
    '0', '.',                     // Keypad
    0, 0, 0,                      // 84-86
    0, 0                          // F11, F12
};

static const char scancode_to_ascii_shift[128] = {
    0,    27,   '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+',
    '\b', '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}',
    '\n', 0,    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0,    '|',  'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,   '*',
    0,    ' ',  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, // F1-F10
    0,    0,    '7', '8', '9', '-', '4', '5', '6', '+', '1', '2', '3', '0',
    '.',  0,    0,   0,   0,   0};

#define SC_ESC         0x01
#define SC_BACKSPACE   0x0E
#define SC_TAB         0x0F
#define SC_ENTER       0x1C
#define SC_LCTRL       0x1D
#define SC_LSHIFT      0x2A
#define SC_RSHIFT      0x36
#define SC_LALT        0x38
#define SC_SPACE       0x39
#define SC_CAPS_LOCK   0x3A
#define SC_F1          0x3B
#define SC_F2          0x3C
#define SC_F3          0x3D
#define SC_F4          0x3E
#define SC_F5          0x3F
#define SC_F6          0x40
#define SC_F7          0x41
#define SC_F8          0x42
#define SC_F9          0x43
#define SC_F10         0x44
#define SC_NUM_LOCK    0x45
#define SC_SCROLL_LOCK 0x46
#define SC_F11         0x57
#define SC_F12         0x58

static void ps2_send_command(uint8_t cmd) {
    while (_inb(PS2_STATUS_PORT) & 0x02)
        ;
    _outb(PS2_DATA_PORT, cmd);
}

void ps2_set_leds(bool scroll, bool num, bool caps) {
    uint8_t led_byte = 0;
    if (scroll)
        led_byte |= PS2_LED_SCROLL_LOCK;
    if (num)
        led_byte |= PS2_LED_NUM_LOCK;
    if (caps)
        led_byte |= PS2_LED_CAPS_LOCK;

    ps2_send_command(PS2_CMD_SET_LEDS);

    for (volatile int i = 0; i < 10000; i++)
        ;

    ps2_send_command(led_byte);
}

static void handle_vt_switch(int fkey) {
    if (fkey >= 1 && fkey <= 6) {
        vt_switch_to(fkey);
    }
}

static uint16_t ps2_scancode_to_mdev(uint8_t scancode, bool is_extended) {
    if (is_extended) {
        switch (scancode) {
        case SC_LCTRL:
            return MDEV_KEY_RIGHTCTRL;
        case SC_LALT:
            return MDEV_KEY_RIGHTALT;
        case 0x47:
            return MDEV_KEY_HOME;
        case 0x48:
            return MDEV_KEY_UP;
        case 0x49:
            return MDEV_KEY_PAGEUP;
        case 0x4B:
            return MDEV_KEY_LEFT;
        case 0x4D:
            return MDEV_KEY_RIGHT;
        case 0x4F:
            return MDEV_KEY_END;
        case 0x50:
            return MDEV_KEY_DOWN;
        case 0x51:
            return MDEV_KEY_PAGEDOWN;
        case 0x52:
            return MDEV_KEY_INSERT;
        case 0x53:
            return MDEV_KEY_DELETE;
        default:
            return 0;
        }
    }

    if (scancode <= SC_F12) {
        return scancode;
    }

    return 0;
}

static void ps2_emit_meowdev_key(uint8_t scancode, bool is_extended,
                                 bool key_released) {
    if (!ps2_meowdev_keyboard) {
        return;
    }

    uint16_t code = ps2_scancode_to_mdev(scancode, is_extended);
    if (code == 0) {
        return;
    }

    size_t state_idx = (size_t)(code & 0x1FF);
    uint32_t value;

    if (key_released) {
        key_down[state_idx] = false;
        value               = 0;
    } else {
        value               = key_down[state_idx] ? 2 : 1;
        key_down[state_idx] = true;
    }

    mdev_add_event(ps2_meowdev_keyboard, MDEV_EVENT_TYPE_KEY, code,
                   (int32_t)value);
}

void ps2_keyboard_handler(registers_t *regs) {
    (void)regs;

    uint8_t scancode = _inb(PS2_DATA_PORT);

    if (scancode == 0xE0) {
        extended = true;
        irq_sendEOI(1);
        return;
    }

    bool key_released  = (scancode & 0x80) != 0;
    scancode          &= 0x7F;

    if (scancode == SC_LSHIFT) {
        ps2_emit_meowdev_key(scancode, extended, key_released);
        modifiers.shift_left = !key_released;
        extended             = false;
        irq_sendEOI(1);
        return;
    }
    if (scancode == SC_RSHIFT) {
        ps2_emit_meowdev_key(scancode, extended, key_released);
        modifiers.shift_right = !key_released;
        extended              = false;
        irq_sendEOI(1);
        return;
    }
    if (scancode == SC_LCTRL) {
        ps2_emit_meowdev_key(scancode, extended, key_released);
        if (extended) {
            modifiers.ctrl_right = !key_released;
        } else {
            modifiers.ctrl_left = !key_released;
        }
        extended = false;
        irq_sendEOI(1);
        return;
    }
    if (scancode == SC_LALT) {
        ps2_emit_meowdev_key(scancode, extended, key_released);
        if (extended) {
            modifiers.alt_right = !key_released;
        } else {
            modifiers.alt_left = !key_released;
        }
        extended = false;
        irq_sendEOI(1);
        return;
    }

    if (!key_released) {
        if (scancode == SC_CAPS_LOCK) {
            ps2_emit_meowdev_key(scancode, extended, key_released);
            modifiers.caps_lock = !modifiers.caps_lock;
            ps2_set_leds(modifiers.scroll_lock, modifiers.num_lock,
                         modifiers.caps_lock);
            extended = false;
            irq_sendEOI(1);
            return;
        }
        if (scancode == SC_NUM_LOCK) {
            ps2_emit_meowdev_key(scancode, extended, key_released);
            modifiers.num_lock = !modifiers.num_lock;
            ps2_set_leds(modifiers.scroll_lock, modifiers.num_lock,
                         modifiers.caps_lock);
            extended = false;
            irq_sendEOI(1);
            return;
        }
        if (scancode == SC_SCROLL_LOCK) {
            ps2_emit_meowdev_key(scancode, extended, key_released);
            modifiers.scroll_lock = !modifiers.scroll_lock;
            ps2_set_leds(modifiers.scroll_lock, modifiers.num_lock,
                         modifiers.caps_lock);
            extended = false;
            irq_sendEOI(1);
            return;
        }
    }

    if (!key_released && (modifiers.alt_left || modifiers.alt_right)) {
        if (scancode >= SC_F1 && scancode <= SC_F6) {
            ps2_emit_meowdev_key(scancode, extended, key_released);
            int fkey = scancode - SC_F1 + 1;
            handle_vt_switch(fkey);
            extended = false;
            irq_sendEOI(1);
            return;
        }
    }

    if (key_released) {
        ps2_emit_meowdev_key(scancode, extended, key_released);
        extended = false;
        irq_sendEOI(1);
        return;
    }

    ps2_emit_meowdev_key(scancode, extended, key_released);

    char ascii = 0;
    bool shift = modifiers.shift_left || modifiers.shift_right;

    if (modifiers.caps_lock && scancode >= 0x10 && scancode <= 0x32) {
        shift = !shift;
    }

    if (shift) {
        ascii = scancode_to_ascii_shift[scancode];
    } else {
        ascii = scancode_to_ascii[scancode];
    }

    if ((modifiers.ctrl_left || modifiers.ctrl_right) && ascii >= 'a' &&
        ascii <= 'z') {
        ascii = ascii - 'a' + 1;
    } else if ((modifiers.ctrl_left || modifiers.ctrl_right) && ascii >= 'A' &&
               ascii <= 'Z') {
        ascii = ascii - 'A' + 1;
    }

    if (ascii != 0) {
        if (active_vt > 0 && vts[active_vt] && vts[active_vt]->tty) {
            tty_t *vt_tty = vts[active_vt]->tty;

            tty_input(vt_tty, ascii);
        }
    }

    extended = false;
    irq_sendEOI(1);
}

void ps2_keyboard_init(void) {
    while (_inb(PS2_STATUS_PORT) & 0x01) {
        _inb(PS2_DATA_PORT);
    }

    modifiers.shift_left  = false;
    modifiers.shift_right = false;
    modifiers.ctrl_left   = false;
    modifiers.ctrl_right  = false;
    modifiers.alt_left    = false;
    modifiers.alt_right   = false;
    modifiers.caps_lock   = false;
    modifiers.num_lock    = false;
    modifiers.scroll_lock = false;
    extended              = false;
    memset(key_down, 0, sizeof(key_down));

    ps2_set_leds(false, false, false);

    irq_registerHandler(1, ps2_keyboard_handler);

    debugf_debug("PS/2 keyboard initialized\n");
}