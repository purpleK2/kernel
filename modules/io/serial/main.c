#include "dev/tty/tty.h"
#include "interrupts/irq.h"
#include "io.h"
#include <stdio.h>
#include "serial.h"

#include <module/modinfo.h>

const modinfo_t modinfo = {.name        = "serial",
                           .version     = "1.0.0",
                           .author      = "NotNekodev",
                           .description = "Serial Port Driver",
                           .license     = "MIT",
                           .url      = "https://github.com/purplek2/PurpleK2",
                           .priority = MOD_PRIO_LOW,
                           .deps = {"kernel", NULL}}; // terminated with a \0

#define MAX_SERIAL_PORTS 4

typedef struct {
    uint16_t base;
    uint8_t irq;
    tty_t *tty;
} serial_port_t;

static serial_port_t serial_ports[MAX_SERIAL_PORTS] = {
    {COM1, COM1_IRQ, NULL},
    {COM2, COM2_IRQ, NULL},
    {COM3, COM3_IRQ, NULL},
    {COM4, COM4_IRQ, NULL},
};

int serial_received(int port) {
    return _inb(port + 5) & 1;
}

int is_transmit_empty(int port) {
    return _inb(port + 5) & 0x20;
}

int com1_write(struct device *dev, const void *buffer, size_t size,
               size_t offset) {
    (void)offset;
    (void)dev;

    for (size_t i = 0; i < size; i++) {
        serial_write(COM1, ((char *)buffer)[i]);
    }

    return 0;
}

int com1_read(struct device *dev, void *buffer, size_t size, size_t offset) {
    (void)offset;
    (void)dev;

    for (size_t i = 0; i < size; i++) {
        ((char *)buffer)[i] = serial_read(COM1);
    }

    return 0;
}

void serial_init(int port) {
    _outb(port + 1, 0x00);
    _outb(port + 3, 0x80);
    _outb(port + 0, 0x03);
    _outb(port + 1, 0x00);
    _outb(port + 3, 0x03);
    _outb(port + 2, 0xC7);
    _outb(port + 4, 0x0B);
    _outb(port + 4, 0x1E);
    _outb(port + 0, 'S');
    if (_inb(port + 0) != 'S') {
        kprintf_warn("Couldn't initialize serial port 0x%llX\n", port);
        return;
    }

    _outb(port + 4, 0x0F);

    _outb(port + 1, 0x01);
}

void serial_write(int port, char c) {
    while (is_transmit_empty(port) == 0)
        ;
    _outb(port, c);
}

char serial_read(int port) {
    while (serial_received(port) == 0)
        ;
    return _inb(port);
}

int com1_ioctl(struct device *dev, int request, void *arg) {
    (void)dev;
    (void)request;
    (void)arg;

    return 0;
}

static ssize_t serial_out(tty_t *tty, const char *buf, size_t size) {
	uint16_t port = (uint16_t)(uintptr_t)tty->priv_data;
	while (size > 0) {
		serial_write(port, *buf);
		size--;
		buf++;
	}
	return size;
}

static tty_ops_t com1_ops = {
    .ioctl = NULL,
    .out = serial_out,
    .cleanup = NULL
};

static void serial_irq_handler(registers_t *regs)
{
    (void)regs;

    for (int i = 0; i < MAX_SERIAL_PORTS; i++) {
        serial_port_t *port = &serial_ports[i];

        if (!port->tty)
            continue;

        if (!(_inb(port->base + 2) & 1)) {
            while (serial_received(port->base)) {
                char c = _inb(port->base);
                tty_input(port->tty, c);
            }
        }
    }

    irq_sendEOI(3);
    irq_sendEOI(4);
}

void module_exit() {
    irq_unregisterHandler(4);
    unregister_device("ttyS0");
}

void module_entry()
{
    irq_registerHandler(3, serial_irq_handler);
    irq_registerHandler(4, serial_irq_handler);

    for (int i = 0; i < MAX_SERIAL_PORTS; i++) {

        uint16_t base = serial_ports[i].base;

        serial_init(base);

        tty_t *tty = tty_create(NULL);
        tty->priv_data = (void *)(uintptr_t)base;
        tty->ops = &com1_ops;

        snprintf(tty->device.name, DEVICE_NAME_MAX,
                 "ttyS%d", i);

        tty->device.dev_node_path = tty->device.name;

        register_device(&tty->device);

        serial_ports[i].tty = tty;
    }
}