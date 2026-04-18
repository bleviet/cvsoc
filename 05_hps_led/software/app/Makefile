# ARM bare-metal application Makefile.
#
# Cross-compiles main.c + startup.S into a flat ELF binary that runs from
# HPS On-Chip RAM (OCRAM) at 0xFFFF0000 on the DE10-Nano Cyclone V SoC.
#
# The compiler is arm-linux-gnueabihf-gcc (hard-float ABI).  It can be
# overridden from the parent Makefile: make CC=arm-linux-gnueabihf-gcc-6

CC      := arm-linux-gnueabihf-gcc

ELF     := hps_led.elf
SRCS    := startup.S main.c
LD_SCRIPT := linker.ld

# -mcpu=cortex-a9     — target the Cortex-A9 core inside the Cyclone V HPS
# -mfpu=neon-vfpv3    — enable NEON/VFPv3 (matches the A9 hardware)
# -mfloat-abi=hard    — pass float arguments in VFP registers (ABI)
# -ffreestanding      — no C standard library startup assumptions
# -nostdlib           — do not link against libc or libgcc implicitly
# -Os                 — optimise for size (fits in 64 KB OCRAM)
CFLAGS  := -mcpu=cortex-a9 -mfpu=neon-vfpv4 -mfloat-abi=hard \
           -ffreestanding -nostdlib \
           -Os -g -Wall -Wextra

LDFLAGS := -T $(LD_SCRIPT) -nostdlib -Wl,--build-id=none

.PHONY: all clean

all: $(ELF)

$(ELF): $(SRCS) $(LD_SCRIPT)
	$(CC) $(CFLAGS) $(LDFLAGS) $(SRCS) -o $@
	arm-linux-gnueabihf-objcopy -O binary $@ hps_led.bin
	arm-linux-gnueabihf-size $@

clean:
	rm -f $(ELF) hps_led.bin
