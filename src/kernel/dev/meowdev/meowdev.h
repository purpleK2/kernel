#ifndef MEOWDEV_H
#define MEOWDEV_H

#include <stdint.h>

#define MEOWDEV_ROOT_PATH "/dev/input"

#define MDEVGRABDEV 0x4D01

#define MDEV_EVENT_TYPE_KEY 1
#define MDEV_EVENT_TYPE_REL 2

#define MDEV_REL_X     0x00
#define MDEV_REL_Y     0x01
#define MDEV_REL_WHEEL 0x08

#define MDEV_BTN_LEFT   0x110
#define MDEV_BTN_RIGHT  0x111
#define MDEV_BTN_MIDDLE 0x112

typedef struct mdev_device mdev_device_t;

struct timeval {
	int64_t tv_sec;
	int64_t tv_usec;
};

typedef struct mdev_event {
	struct timeval timestamp;
	uint16_t type;
	uint16_t code;
	uint32_t value;
} __attribute__((packed)) mdev_event_t;

typedef enum mdev_device_kind {
	MDEV_DEVICE_KEYBOARD = 0,
	MDEV_DEVICE_MOUSE    = 1,
} mdev_device_kind_t;

typedef enum mdev_key_code {
	MDEV_KEY_ESC         = 0x01,
	MDEV_KEY_1           = 0x02,
	MDEV_KEY_2           = 0x03,
	MDEV_KEY_3           = 0x04,
	MDEV_KEY_4           = 0x05,
	MDEV_KEY_5           = 0x06,
	MDEV_KEY_6           = 0x07,
	MDEV_KEY_7           = 0x08,
	MDEV_KEY_8           = 0x09,
	MDEV_KEY_9           = 0x0A,
	MDEV_KEY_0           = 0x0B,
	MDEV_KEY_MINUS       = 0x0C,
	MDEV_KEY_EQUAL       = 0x0D,
	MDEV_KEY_BACKSPACE   = 0x0E,
	MDEV_KEY_TAB         = 0x0F,
	MDEV_KEY_Q           = 0x10,
	MDEV_KEY_W           = 0x11,
	MDEV_KEY_E           = 0x12,
	MDEV_KEY_R           = 0x13,
	MDEV_KEY_T           = 0x14,
	MDEV_KEY_Y           = 0x15,
	MDEV_KEY_U           = 0x16,
	MDEV_KEY_I           = 0x17,
	MDEV_KEY_O           = 0x18,
	MDEV_KEY_P           = 0x19,
	MDEV_KEY_LEFTBRACE   = 0x1A,
	MDEV_KEY_RIGHTBRACE  = 0x1B,
	MDEV_KEY_ENTER       = 0x1C,
	MDEV_KEY_LEFTCTRL    = 0x1D,
	MDEV_KEY_A           = 0x1E,
	MDEV_KEY_S           = 0x1F,
	MDEV_KEY_D           = 0x20,
	MDEV_KEY_F           = 0x21,
	MDEV_KEY_G           = 0x22,
	MDEV_KEY_H           = 0x23,
	MDEV_KEY_J           = 0x24,
	MDEV_KEY_K           = 0x25,
	MDEV_KEY_L           = 0x26,
	MDEV_KEY_SEMICOLON   = 0x27,
	MDEV_KEY_APOSTROPHE  = 0x28,
	MDEV_KEY_GRAVE       = 0x29,
	MDEV_KEY_LEFTSHIFT   = 0x2A,
	MDEV_KEY_BACKSLASH   = 0x2B,
	MDEV_KEY_Z           = 0x2C,
	MDEV_KEY_X           = 0x2D,
	MDEV_KEY_C           = 0x2E,
	MDEV_KEY_V           = 0x2F,
	MDEV_KEY_B           = 0x30,
	MDEV_KEY_N           = 0x31,
	MDEV_KEY_M           = 0x32,
	MDEV_KEY_COMMA       = 0x33,
	MDEV_KEY_DOT         = 0x34,
	MDEV_KEY_SLASH       = 0x35,
	MDEV_KEY_RIGHTSHIFT  = 0x36,
	MDEV_KEY_KPASTERISK  = 0x37,
	MDEV_KEY_LEFTALT     = 0x38,
	MDEV_KEY_SPACE       = 0x39,
	MDEV_KEY_CAPSLOCK    = 0x3A,
	MDEV_KEY_F1          = 0x3B,
	MDEV_KEY_F2          = 0x3C,
	MDEV_KEY_F3          = 0x3D,
	MDEV_KEY_F4          = 0x3E,
	MDEV_KEY_F5          = 0x3F,
	MDEV_KEY_F6          = 0x40,
	MDEV_KEY_F7          = 0x41,
	MDEV_KEY_F8          = 0x42,
	MDEV_KEY_F9          = 0x43,
	MDEV_KEY_F10         = 0x44,
	MDEV_KEY_NUMLOCK     = 0x45,
	MDEV_KEY_SCROLLLOCK  = 0x46,
	MDEV_KEY_F11         = 0x57,
	MDEV_KEY_F12         = 0x58,

	MDEV_KEY_RIGHTCTRL   = 0x11D,
	MDEV_KEY_RIGHTALT    = 0x138,
	MDEV_KEY_HOME        = 0x147,
	MDEV_KEY_UP          = 0x148,
	MDEV_KEY_PAGEUP      = 0x149,
	MDEV_KEY_LEFT        = 0x14B,
	MDEV_KEY_RIGHT       = 0x14D,
	MDEV_KEY_END         = 0x14F,
	MDEV_KEY_DOWN        = 0x150,
	MDEV_KEY_PAGEDOWN    = 0x151,
	MDEV_KEY_INSERT      = 0x152,
	MDEV_KEY_DELETE      = 0x153,
} mdev_key_code_t;

int meowdev_init(void);
int meowdev_deinit(void);

mdev_device_t *mdev_register_device(mdev_device_kind_t kind);
mdev_device_t *mdev_register_keyboard(void);
mdev_device_t *mdev_register_mouse(void);

int mdev_add_event(mdev_device_t *dev, uint16_t type, uint16_t code,
				   int32_t value);

#endif // MEOWDEV_H