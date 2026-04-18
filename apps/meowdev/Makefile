.PHONY: all clean

include ../userspace.mk

TARGET = meowdev_test.elf
C_SRCS = main.c

OBJS = $(C_SRCS:.c=.o)

LDFLAGS =

all: $(TARGET)

$(TARGET): $(OBJS)
	$(USERSPACE_LINK)

clean:
	rm -f $(OBJS) $(TARGET)
