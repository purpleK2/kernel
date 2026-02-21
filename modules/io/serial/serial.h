#ifndef SERIAL_H
#define SERIAL_H 1

#include <dev/device.h>

#define COM1 0x3F8
#define COM2 0x2F8
#define COM3 0x3E8
#define COM4 0x2E8

#define COM1_IRQ 4
#define COM2_IRQ 3
#define COM3_IRQ 4
#define COM4_IRQ 3

int com1_write(struct device *dev, const void *buffer, size_t size,
               size_t offset);
int com1_read(struct device *dev, void *buffer, size_t size, size_t offset);
int com1_ioctl(struct device *dev, int request, void *arg);

void serial_init(int port);

void serial_write(int port, char c);
char serial_read(int port);

#endif // SERIAL_H