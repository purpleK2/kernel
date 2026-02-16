CC = gcc
LD = ld
CFLAGS = -O0 -g -Wall -Wextra -ffreestanding -fno-builtin \
         -fno-pic -fno-pie -m64 -nostdlib -mno-red-zone \
         -mcmodel=large -fno-stack-protector \
         -fno-omit-frame-pointer -mno-sse -mno-mmx \
         -fno-plt -fno-jump-tables \
         -I. \
         -I../../../src \
         -I../../../src/lib \
         -I../../../src/kernel \
         -I../../../src/kernel/system \
         -I../../../src/kernel/acpi \
         -I../../../src/arch/x86_64
LDFLAGS = -r -nostdlib -z norelro

TARGET = ps2.km
SRC = ps2.c ps2_kb.c
OBJ = $(SRC:.c=.o)

.PHONY: all clean

all: $(TARGET)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJ)
	$(LD) $(LDFLAGS) $(OBJ) -o $(TARGET)

clean:
	rm -f *.o $(TARGET)