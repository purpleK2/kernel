.PHONY: all clean

include ../userspace.mk

TARGET = signal_test.elf
C_SRCS = main.c
OBJS = $(C_SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(USERSPACE_LINK)

clean:
	rm -f $(OBJS) $(TARGET)
