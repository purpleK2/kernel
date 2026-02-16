#include <stdio.h>

#include <module/modinfo.h>

#include "ps2.h"

const modinfo_t modinfo = {.name        = "ps2",
                           .version     = "1.0.0",
                           .author      = "NotNekodev",
                           .description = "Contains drivers for the PS/2 keyboard and mouse",
                           .license     = "MIT",
                           .url      = "https://github.com/purplek2/PurpleK2",
                           .priority = MOD_PRIO_LOW,
                           .deps = {"kernel", NULL}}; // terminated with a \0

void module_exit() {
}

void module_entry() {
    ps2_keyboard_init();

}