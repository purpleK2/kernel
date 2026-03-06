.PHONY: all clean

include ../userspace.mk

TARGET = poll_test.elf
C_SRCS = main.c
ASM_SRCS = scrt.asm

OBJS = $(C_SRCS:.c=.o) $(ASM_SRCS:.asm=.o)

LDFLAGS = $(LDFLAGS_BASE) -o $(TARGET)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(LD) $(OBJS) $(LDFLAGS)

clean:
	rm -f $(OBJS) $(TARGET)
