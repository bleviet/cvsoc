# ARM bare-metal interrupt application Makefile.
#
# Cross-compiles startup.S + main.c into a flat binary that runs from
# HPS On-Chip RAM (OCRAM) at 0xFFFF0000 on the DE10-Nano Cyclone V SoC.

CC      := arm-linux-gnueabihf-gcc

ELF       := hps_interrupts.elf
SRCS      := startup.S main.c
LD_SCRIPT := linker.ld

CFLAGS  := -mcpu=cortex-a9 -mfpu=neon-vfpv4 -mfloat-abi=hard \
           -ffreestanding -nostdlib \
           -Os -g -Wall -Wextra

LDFLAGS := -T $(LD_SCRIPT) -nostdlib -Wl,--build-id=none

.PHONY: all clean

all: $(ELF)

$(ELF): $(SRCS) $(LD_SCRIPT)
	$(CC) $(CFLAGS) $(LDFLAGS) $(SRCS) -o $@
	arm-linux-gnueabihf-objcopy -O binary $@ hps_interrupts.bin
	arm-linux-gnueabihf-size $@

clean:
	rm -f $(ELF) hps_interrupts.bin
