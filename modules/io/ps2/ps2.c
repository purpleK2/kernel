#include <stdio.h>

#include <interrupts/irq.h>
#include <module/modinfo.h>

#include "ps2.h"

mdev_device_t *ps2_meowdev_keyboard = NULL;
mdev_device_t *ps2_meowdev_mouse    = NULL;

const modinfo_t modinfo = {
    .name        = "ps2",
    .version     = "1.0.0",
    .author      = "NotNekodev",
    .description = "Contains drivers for the PS/2 keyboard and mouse",
    .license     = "MIT",
    .url         = "https://github.com/purplek2/PurpleK2",
    .priority    = MOD_PRIO_LOW,
    .deps        = {"kernel", NULL}}; // terminated with a \0

void module_exit() {
    irq_unregisterHandler(1);
    irq_unregisterHandler(12);
}

void module_entry() {
    ps2_meowdev_keyboard = mdev_register_keyboard();
    if (!ps2_meowdev_keyboard) {
        debugf_warn("PS/2: failed to register meowdev keyboard\n");
    }

    ps2_meowdev_mouse = mdev_register_mouse();
    if (!ps2_meowdev_mouse) {
        debugf_warn("PS/2: failed to register meowdev mouse\n");
    }

    ps2_keyboard_init();
    ps2_mouse_init();
}