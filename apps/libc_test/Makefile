.PHONY: all clean

include ../userspace.mk

TARGET = libc_test.elf
C_SRCS = main.c

OBJS = $(C_SRCS:.c=.o)

SYSROOT = $(USERSPACE_SYSROOT)
LDFLAGS = -static \
	$(SYSROOT)/usr/lib/Scrt1.o \
	$(SYSROOT)/usr/lib/crti.o \
	-lc \
	$(SYSROOT)/usr/lib/crtn.o \
	-o $(TARGET)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(LD) $(OBJS) $(LDFLAGS)

clean:
	rm -f $(OBJS) $(TARGET)
