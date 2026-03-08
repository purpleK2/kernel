.PHONY: all clean

include ../userspace.mk

TARGET = libc_test.elf
C_SRCS = main.c

OBJS = $(C_SRCS:.c=.o)

LDFLAGS = $(LDFLAGS_BASE) -o $(TARGET)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(LD) $(OBJS) $(LDFLAGS)

clean:
	rm -f $(OBJS) $(TARGET)
