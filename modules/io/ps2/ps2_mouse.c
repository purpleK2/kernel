#include "ps2.h"

#include "interrupts/irq.h"
#include "io.h"
#include "stdio.h"

#define PS2_IO_TIMEOUT 100000

typedef struct {
    bool left;
    bool right;
    bool middle;
} ps2_mouse_buttons_t;

static uint8_t mouse_packet[3]           = {0};
static uint8_t mouse_packet_index        = 0;
static ps2_mouse_buttons_t mouse_buttons = {0};

static bool ps2_wait_input_clear(void) {
    for (int i = 0; i < PS2_IO_TIMEOUT; i++) {
        if (!(_inb(PS2_STATUS_PORT) & PS2_STATUS_INPUT_FULL)) {
            return true;
        }
    }

    return false;
}

static bool ps2_wait_output_full(void) {
    for (int i = 0; i < PS2_IO_TIMEOUT; i++) {
        if (_inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) {
            return true;
        }
    }

    return false;
}

static bool ps2_read_data(uint8_t *data) {
    if (!data) {
        return false;
    }

    if (!ps2_wait_output_full()) {
        return false;
    }

    *data = _inb(PS2_DATA_PORT);
    return true;
}

static bool ps2_write_command(uint8_t command) {
    if (!ps2_wait_input_clear()) {
        return false;
    }

    _outb(PS2_COMMAND_PORT, command);
    return true;
}

static bool ps2_write_data(uint8_t data) {
    if (!ps2_wait_input_clear()) {
        return false;
    }

    _outb(PS2_DATA_PORT, data);
    return true;
}

static bool ps2_write_mouse(uint8_t command) {
    if (!ps2_write_command(PS2_CMD_WRITE_TO_AUX)) {
        return false;
    }

    if (!ps2_write_data(command)) {
        return false;
    }

    return true;
}

static bool ps2_read_aux_byte(uint8_t *data) {
    if (!data) {
        return false;
    }

    for (int i = 0; i < PS2_IO_TIMEOUT; i++) {
        uint8_t status = _inb(PS2_STATUS_PORT);
        if (!(status & PS2_STATUS_OUTPUT_FULL)) {
            continue;
        }

        uint8_t value = _inb(PS2_DATA_PORT);
        if (status & PS2_STATUS_AUX_DATA) {
            *data = value;
            return true;
        }
    }

    return false;
}

static bool ps2_mouse_send_expect_ack(uint8_t command) {
    uint8_t resp = 0;

    if (!ps2_write_mouse(command)) {
        return false;
    }

    if (!ps2_read_aux_byte(&resp)) {
        return false;
    }

    return resp == PS2_ACK;
}

static void ps2_mouse_emit_button(uint16_t code, bool now_pressed,
                                  bool *prev_pressed) {
    if (!prev_pressed || !ps2_meowdev_mouse) {
        return;
    }

    if (*prev_pressed == now_pressed) {
        return;
    }

    *prev_pressed = now_pressed;
    mdev_add_event(ps2_meowdev_mouse, MDEV_EVENT_TYPE_KEY, code,
                   now_pressed ? 1 : 0);
}

static void ps2_mouse_emit_motion(int32_t dx, int32_t dy) {
    if (!ps2_meowdev_mouse) {
        return;
    }

    if (dx != 0) {
        mdev_add_event(ps2_meowdev_mouse, MDEV_EVENT_TYPE_REL, MDEV_REL_X, dx);
    }

    if (dy != 0) {
        mdev_add_event(ps2_meowdev_mouse, MDEV_EVENT_TYPE_REL, MDEV_REL_Y, dy);
    }
}

void ps2_mouse_handler(registers_t *regs) {
    (void)regs;

    uint8_t status = _inb(PS2_STATUS_PORT);
    if (!(status & PS2_STATUS_OUTPUT_FULL) || !(status & PS2_STATUS_AUX_DATA)) {
        irq_sendEOI(12);
        return;
    }

    uint8_t data = _inb(PS2_DATA_PORT);

    if (mouse_packet_index == 0 && !(data & 0x08)) {
        irq_sendEOI(12);
        return;
    }

    mouse_packet[mouse_packet_index++] = data;
    if (mouse_packet_index < 3) {
        irq_sendEOI(12);
        return;
    }

    mouse_packet_index = 0;

    if ((mouse_packet[0] & 0xC0) != 0) {
        irq_sendEOI(12);
        return;
    }

    int32_t dx = (int32_t)((int8_t)mouse_packet[1]);
    int32_t dy = -(int32_t)((int8_t)mouse_packet[2]);

    bool left_pressed   = (mouse_packet[0] & 0x01) != 0;
    bool right_pressed  = (mouse_packet[0] & 0x02) != 0;
    bool middle_pressed = (mouse_packet[0] & 0x04) != 0;

    ps2_mouse_emit_button(MDEV_BTN_LEFT, left_pressed, &mouse_buttons.left);
    ps2_mouse_emit_button(MDEV_BTN_RIGHT, right_pressed, &mouse_buttons.right);
    ps2_mouse_emit_button(MDEV_BTN_MIDDLE, middle_pressed,
                          &mouse_buttons.middle);
    ps2_mouse_emit_motion(dx, dy);

    irq_sendEOI(12);
}

void ps2_mouse_init(void) {
    mouse_packet_index   = 0;
    mouse_buttons.left   = false;
    mouse_buttons.right  = false;
    mouse_buttons.middle = false;

    while (_inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) {
        _inb(PS2_DATA_PORT);
    }

    if (!ps2_write_command(PS2_CMD_ENABLE_AUX_PORT)) {
        debugf_warn("PS/2 mouse: failed to enable AUX port\n");
        return;
    }

    if (!ps2_write_command(PS2_CMD_READ_CONFIG)) {
        debugf_warn("PS/2 mouse: failed to request controller config\n");
        return;
    }

    uint8_t config = 0;
    if (!ps2_read_data(&config)) {
        debugf_warn("PS/2 mouse: failed to read controller config\n");
        return;
    }

    config |= 0x02;
    config &= (uint8_t)~0x20;

    if (!ps2_write_command(PS2_CMD_WRITE_CONFIG) || !ps2_write_data(config)) {
        debugf_warn("PS/2 mouse: failed to write controller config\n");
        return;
    }

    if (!ps2_mouse_send_expect_ack(PS2_MOUSE_CMD_SET_DEFAULTS)) {
        debugf_warn("PS/2 mouse: failed to set defaults\n");
        return;
    }

    if (!ps2_mouse_send_expect_ack(PS2_MOUSE_CMD_ENABLE_REPORTS)) {
        debugf_warn("PS/2 mouse: failed to enable streaming\n");
        return;
    }

    irq_registerHandler(12, ps2_mouse_handler);
    debugf_debug("PS/2 mouse initialized\n");
}
