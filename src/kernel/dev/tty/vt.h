#ifndef VT_H
#define VT_H 1

#include <dev/tty/tty.h>
#include <stdbool.h>
#include <stdint.h>

#define VT_ROWS 25
#define VT_COLS 80
#define MAX_VTS 64

enum {
    VT_STATE_NORMAL = 0,
    VT_STATE_ESC,
    VT_STATE_CSI,
    VT_STATE_CSI_PARAM,
};

typedef struct vt {
    tty_t *tty;
    
    int escape_state;
    char escape_buf[32];
    int escape_idx;
    int params[16];
    int param_count;
    
    bool active;
    int vt_num;
} vt_t;

extern vt_t *vts[MAX_VTS];
extern int active_vt;

void vt_init(void);
vt_t *vt_create(int vt_num);
void vt_destroy(vt_t *vt);

void vt_switch_to(int vt_num);

void vt_handle_escape(vt_t *vt, char c);
void vt_execute_csi(vt_t *vt, char cmd);

uint32_t vt_ansi_to_rgb(int color, bool bright);

void vt_take_over_console(void);
bool vt_is_console_active(void);

#endif // VT_H