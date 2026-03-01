.PHONY: all clean

TARGET = test.elf
C_SRCS = main.c
ASM_SRCS = scrt.asm

OBJS = $(C_SRCS:.c=.o) $(ASM_SRCS:.asm=.o)

CC = clang
FASM = fasm

CFLAGS = -target x86_64-unknown-none \
         -ffreestanding \
         -fno-stack-protector \
         -fPIE \
         -O0 \
         -g \
         -nostdlib \
         -Wall \
         -Wextra

LDFLAGS = -nostdlib \
          -pie \
          --entry=_start \
          -o $(TARGET)

all: $(TARGET)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.asm
	$(FASM) $< $@

$(TARGET): $(OBJS)
	ld.lld $(OBJS) $(LDFLAGS)

clean:
	rm -f $(OBJS) $(TARGET)
