#include "vt.h"
#include "dev/device.h"
#include "errors.h"
#include "memory/heap/kheap.h"
#include "scheduler/scheduler.h"
#include "stdio.h"
#include "terminal/terminal.h"
#include "uaccess.h"
#include <string.h>
#include <util/assert.h>

vt_t *vts[MAX_VTS];
int active_vt = 0;

bool kernel_console_active = true;

static const uint32_t ansi_colors[16] = {
    0x000000,
    0xAA0000,
    0x00AA00,
    0xAA5500,
    0x0000AA,
    0xAA00AA,
    0x00AAAA,
    0xAAAAAA,
    0x555555,
    0xFF5555,
    0x55FF55,
    0xFFFF55,
    0x5555FF,
    0xFF55FF,
    0x55FFFF,
    0xFFFFFF,
};

uint32_t vt_ansi_to_rgb(int color, bool bright) {
    if (color < 0 || color > 7)
        return ansi_colors[7];
    
    return ansi_colors[color + (bright ? 8 : 0)];
}

void vt_execute_csi(vt_t *vt, char cmd) {
    if (!vt || !vt->active)
        return;
    
    int n = (vt->param_count > 0 && vt->params[0] > 0) ? vt->params[0] : 1;
    
    switch (cmd) {
    case 'J':
        if (n == 0 || n == 2) {
            _term_cls();
        }
        break;
        
    case 'm':
        for (int i = 0; i < vt->param_count; i++) {
            int p = vt->params[i];
            
            if (p == 0) {
                _term_set_fg(0xAAAAAA);
                _term_set_bg(0x000000);
            } else if (p >= 30 && p <= 37) {
                _term_set_fg(vt_ansi_to_rgb(p - 30, false));
            } else if (p >= 40 && p <= 47) {
                _term_set_bg(vt_ansi_to_rgb(p - 40, false));
            } else if (p >= 90 && p <= 97) {
                _term_set_fg(vt_ansi_to_rgb(p - 90, true));
            } else if (p >= 100 && p <= 107) {
                _term_set_bg(vt_ansi_to_rgb(p - 100, true));
            }
        }
        break;
        
    case 'A':
    case 'B':
    case 'C':
    case 'D':
    case 'H':
    case 'f':
    case 'K':
    case 's':
    case 'u':
    case 'h':
    case 'l':
        break;
    }
}

void vt_handle_escape(vt_t *vt, char c) {
    if (!vt)
        return;
    
    switch (vt->escape_state) {
    case VT_STATE_ESC:
        if (c == '[') {
            vt->escape_state = VT_STATE_CSI;
            vt->escape_idx = 0;
            vt->param_count = 0;
            memset(vt->params, 0, sizeof(vt->params));
        } else {
            vt->escape_state = VT_STATE_NORMAL;
        }
        break;
        
    case VT_STATE_CSI:
    case VT_STATE_CSI_PARAM:
        if (c >= '0' && c <= '9') {
            if (vt->param_count == 0)
                vt->param_count = 1;
            vt->params[vt->param_count - 1] = 
                vt->params[vt->param_count - 1] * 10 + (c - '0');
            vt->escape_state = VT_STATE_CSI_PARAM;
        } else if (c == ';') {
            if (vt->param_count < 16)
                vt->param_count++;
            vt->escape_state = VT_STATE_CSI_PARAM;
        } else if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
            if (vt->param_count == 0)
                vt->param_count = 1;
            vt_execute_csi(vt, c);
            vt->escape_state = VT_STATE_NORMAL;
        } else {
            vt->escape_state = VT_STATE_NORMAL;
        }
        break;
        
    default:
        vt->escape_state = VT_STATE_NORMAL;
        break;
    }
}

static ssize_t vt_output(tty_t *tty, const char *buf, size_t size) {
    vt_t *vt = (vt_t *)tty->priv_data;
    if (!vt || !vt->active)
        return 0;
    
    for (size_t i = 0; i < size; i++) {
        _term_putc(buf[i]);
    }
    
    return size;
}

static tty_ops_t vt_tty_ops = {
    .ioctl = NULL,
    .out = vt_output,
    .cleanup = NULL
};

static int vt_ioctl_handler(tty_t *tty, long request, void *arg) {
    (void)tty;
    int ret;
    
    switch (request) {
    case VT_ACTIVATE: {
        int vt_num;
        ret = copy_from_user(&vt_num, arg, sizeof(int));
        if (ret != 0) {
            return -EFAULT;
        }
        
        if (vt_num < 1 || vt_num >= MAX_VTS) {
            return -EINVAL;
        }
        
        if (!vts[vt_num]) {
            return -ENXIO;
        }
        
        vt_switch_to(vt_num);
        return 0;
    }
    
    case VT_GETSTATE: {
        vt_stat_t state;
        
        state.v_active = active_vt;
        state.v_signal = 0;
        state.v_state = 0;
        
        for (int i = 1; i < MAX_VTS; i++) {
            if (vts[i]) {
                state.v_state |= (1 << i);
            }
        }
        
        ret = copy_to_user(arg, &state, sizeof(vt_stat_t));
        if (ret != 0) {
            return -EFAULT;
        }
        
        return 0;
    }
    
    case VT_OPENQRY: {
        int vt_num = -1;
        
        for (int i = 1; i < MAX_VTS; i++) {
            if (!vts[i]) {
                vt_num = i;
                break;
            }
        }
        
        if (vt_num == -1) {
            return -ENXIO;
        }
        
        ret = copy_to_user(arg, &vt_num, sizeof(int));
        if (ret != 0) {
            return -EFAULT;
        }
        
        return 0;
    }
    
    case VT_WAITACTIVE: {
        int vt_num;
        ret = copy_from_user(&vt_num, arg, sizeof(int));
        if (ret != 0) {
            return -EFAULT;
        }
        
        if (vt_num < 1 || vt_num >= MAX_VTS) {
            return -EINVAL;
        }
        
        if (active_vt == vt_num) {
            return 0;
        }
        
        return -EINTR;
    }
    
    default:
        return -EINVAL;
    }
}

static int tty0_ioctl(struct device *dev, int request, void *arg) {
    (void)dev;
    
    if (request == VT_ACTIVATE || request == VT_GETSTATE || 
        request == VT_OPENQRY || request == VT_WAITACTIVE) {
        return vt_ioctl_handler(NULL, request, arg);
    }
    
    if (active_vt > 0 && vts[active_vt] && vts[active_vt]->tty) {
        return vts[active_vt]->tty->device.ioctl(&vts[active_vt]->tty->device, request, arg);
    }
    
    return -EINVAL;
}

static ssize_t tty0_output(tty_t *tty, const char *buf, size_t size) {
    (void)tty;
    
    if (active_vt > 0 && vts[active_vt] && vts[active_vt]->tty) {
        return vts[active_vt]->tty->ops->out(vts[active_vt]->tty, buf, size);
    }
    
    return 0;
}

static int tty0_read(struct device *dev, void *buffer, size_t size, size_t offset) {
    (void)dev;
    (void)offset;
    
    if (active_vt > 0 && vts[active_vt] && vts[active_vt]->tty) {
        return vts[active_vt]->tty->device.read(&vts[active_vt]->tty->device, buffer, size, offset);
    }
    
    return 0;
}

static tty_ops_t tty0_ops = {
    .ioctl = NULL,
    .out = tty0_output,
    .cleanup = NULL
};

void vt_create_tty0(void) {
    tty_t *tty0 = tty_create(NULL);
    if (!tty0) {
        debugf_warn("Failed to create tty0\n");
        return;
    }
    
    tty0->ops = &tty0_ops;
    tty0->device.read = tty0_read;
    tty0->device.ioctl = tty0_ioctl;
    
    snprintf(tty0->device.name, DEVICE_NAME_MAX, "tty0");
    tty0->device.dev_node_path = "tty0";
    register_device(&tty0->device);
    
    debugf_debug("Created tty0 (current VT)\n");
}

static int console_ioctl(struct device *dev, int request, void *arg) {
    (void)dev;
    
    if (request == VT_ACTIVATE || request == VT_GETSTATE || 
        request == VT_OPENQRY || request == VT_WAITACTIVE) {
        return vt_ioctl_handler(NULL, request, arg);
    }
    
    if (active_vt > 0 && vts[active_vt] && vts[active_vt]->tty) {
        return vts[active_vt]->tty->device.ioctl(&vts[active_vt]->tty->device, request, arg);
    }
    
    return -EINVAL;
}

static int console_read(struct device *dev, void *buffer, size_t size, size_t offset) {
    (void)dev;
    (void)offset;
    
    if (active_vt > 0 && vts[active_vt] && vts[active_vt]->tty) {
        return vts[active_vt]->tty->device.read(&vts[active_vt]->tty->device, buffer, size, offset);
    }
    
    return 0;
}

static ssize_t console_output(tty_t *tty, const char *buf, size_t size) {
    (void)tty;
    
    for (size_t i = 0; i < size; i++) {
        _term_putc(buf[i]);
    }
    
    return size;
}

static tty_ops_t console_ops = {
    .ioctl = NULL,
    .out = console_output,
    .cleanup = NULL
};

void vt_create_console(void) {
    tty_t *console = tty_create(NULL);
    if (!console) {
        debugf_warn("Failed to create console\n");
        return;
    }
    
    console->ops = &console_ops;
    console->device.read = console_read;
    console->device.ioctl = console_ioctl;
    
    snprintf(console->device.name, DEVICE_NAME_MAX, "console");
    console->device.dev_node_path = "console";
    register_device(&console->device);
    
    debugf_debug("Created console\n");
}

static int ctty_read(struct device *dev, void *buffer, size_t size, size_t offset) {
    (void)dev;
    (void)offset;
    
    pcb_t *proc = get_current_pcb();
    if (!proc || !proc->ctty) {
        return -ENXIO;
    }
    
    tty_t *ctty = (tty_t *)proc->ctty;
    return ctty->device.read(&ctty->device, buffer, size, offset);
}

static int ctty_write(struct device *dev, const void *buffer, size_t size, size_t offset) {
    (void)dev;
    (void)offset;
    
    pcb_t *proc = get_current_pcb();
    if (!proc || !proc->ctty) {
        return -ENXIO;
    }
    
    tty_t *ctty = (tty_t *)proc->ctty;
    return ctty->device.write(&ctty->device, buffer, size, offset);
}

static int ctty_ioctl(struct device *dev, int request, void *arg) {
    (void)dev;
    
    pcb_t *proc = get_current_pcb();
    if (!proc || !proc->ctty) {
        return -ENXIO;
    }
    
    tty_t *ctty = (tty_t *)proc->ctty;
    return ctty->device.ioctl(&ctty->device, request, arg);
}

static ssize_t ctty_output(tty_t *tty, const char *buf, size_t size) {
    (void)tty;
    
    pcb_t *proc = get_current_pcb();
    if (!proc || !proc->ctty) {
        return -ENXIO;
    }
    
    tty_t *ctty = (tty_t *)proc->ctty;
    if (ctty->ops && ctty->ops->out) {
        return ctty->ops->out(ctty, buf, size);
    }
    
    return 0;
}

static tty_ops_t ctty_ops = {
    .ioctl = NULL,
    .out = ctty_output,
    .cleanup = NULL
};

void vt_create_ctty(void) {
    tty_t *ctty_dev = tty_create(NULL);
    if (!ctty_dev) {
        debugf_warn("Failed to create /dev/tty\n");
        return;
    }
    
    ctty_dev->ops = &ctty_ops;
    ctty_dev->device.read = ctty_read;
    ctty_dev->device.write = ctty_write;
    ctty_dev->device.ioctl = ctty_ioctl;
    
    snprintf(ctty_dev->device.name, DEVICE_NAME_MAX, "tty");
    ctty_dev->device.dev_node_path = "tty";
    register_device(&ctty_dev->device);
    
    debugf_debug("Created /dev/tty (controlling terminal)\n");
}

vt_t *vt_create(int vt_num) {
    if (vt_num < 1 || vt_num >= MAX_VTS)
        return NULL;
    
    vt_t *vt = kmalloc(sizeof(vt_t));
    if (!vt)
        return NULL;
    
    memset(vt, 0, sizeof(vt_t));
    
    vt->tty = tty_create(NULL);
    if (!vt->tty) {
        kfree(vt);
        return NULL;
    }
    
    vt->tty->priv_data = vt;
    vt->tty->ops = &vt_tty_ops;
    
    vt->vt_num = vt_num;
    vt->escape_state = VT_STATE_NORMAL;
    vt->active = false;
    
    snprintf(vt->tty->device.name, DEVICE_NAME_MAX, "tty%d", vt_num);
    vt->tty->device.dev_node_path = vt->tty->device.name;
    register_device(&vt->tty->device);
    
    return vt;
}

void vt_destroy(vt_t *vt) {
    if (!vt)
        return;
    
    if (vt->tty) {
        kfree(vt->tty);
    }
    
    kfree(vt);
}

void vt_switch_to(int vt_num) {
    if (vt_num < 1 || vt_num >= MAX_VTS)
        return;
    
    if (!vts[vt_num]) {
        debugf_warn("VT %d does not exist\n", vt_num);
        return;
    }
    
    if (active_vt > 0 && vts[active_vt]) {
        vts[active_vt]->active = false;
    }
    
    active_vt = vt_num;
    vts[vt_num]->active = true;
    
    _term_cls();
    _term_set_fg(0xAAAAAA);
    _term_set_bg(0x000000);
    
    debugf_debug("Switched to VT%d\n", vt_num);
}

void vt_take_over_console(void) {
    kernel_console_active = false;
    debugf_debug("Kernel console disabled, VTs now active\n");
}

bool vt_is_console_active(void) {
    return kernel_console_active;
}

void vt_init(void) {
    for (int i = 1; i <= MAX_VTS - 1; i++) {
        vts[i] = vt_create(i);
        if (!vts[i]) {
            debugf_warn("Failed to create VT%d\n", i);
        }
    }

    vt_create_tty0();
    vt_create_console();
    vt_create_ctty();
    
    if (vts[1]) {
        active_vt = 1;
        vts[1]->active = true;
        
        _term_cls();
        
        vt_take_over_console();
        
        debugf_debug("VT system initialized, VT1 active\n");
    }
}