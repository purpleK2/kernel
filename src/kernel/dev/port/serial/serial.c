#include "serial.h"
#include "dev/tty/tty.h"
#include "interrupts/irq.h"

#include <dev/device.h>
#include <io.h>

#include <stdio.h>

static tty_t *com1_tty;

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
    _outb(port + 1, 0x00); // disable interrupts
    _outb(port + 3, 0x80); // enable DLAB
    _outb(port + 0, 0x03); // set divisor to 3 (lo byte) 38400 baud
    _outb(port + 1, 0x00); //                  (hi byte)
    _outb(port + 3, 0x03); // 8 bits, no parity, one stop bit
    _outb(port + 2,
          0xC7);           // enable FIFO, clear them, with 14-byte threshold
    _outb(port + 4, 0x0B); // IRQs enabled, RTS/DSR set
    _outb(port + 4, 0x1E); // set in loopback mode, test the serial chip
    _outb(port + 0, 'S');  // test serial chip (send byte 0xAE and check if
                           // serial returns same byte)
    if (_inb(port + 0) != 'S') {
        kprintf_warn("Couldn't initialize serial port 0x%llX\n", port);
        return;
    }

    // If serial is not looped back, set it in normal operation mode
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

    // Implement IOCTL commands here if needed
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

void com1_irq_handler(registers_t *regs) {
    (void)regs;

    uint16_t port = (uint16_t)(uintptr_t)com1_tty->priv_data;

    while (serial_received(port)) {
        char c = _inb(port);
        tty_input(com1_tty, c);
    }

    irq_sendEOI(4);
}

void dev_serial_init() {
    serial_init(COM1);

    tty_t *serial_tty = tty_create(NULL);
    serial_tty->priv_data = (void *)(uintptr_t)COM1;
    serial_tty->ops = &com1_ops;

    snprintf(serial_tty->device.name, DEVICE_NAME_MAX, "ttyS0");
    serial_tty->device.dev_node_path = "ttyS0";

    register_device(&serial_tty->device);

    com1_tty = serial_tty;

    irq_registerHandler(4, com1_irq_handler);
}
