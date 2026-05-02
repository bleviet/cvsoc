# ARM bare-metal application Makefile for DDR3 HPS memory test.
#
# Cross-compiles main.c + startup.S into a flat binary that runs from
# DDR3 SDRAM at 0x00100000 on the DE10-Nano Cyclone V SoC.
#
# IMPORTANT: The DDR3 controller must be initialised by U-Boot SPL before
# loading this application.

CC      := arm-linux-gnueabihf-gcc

ELF     := ddr3_hps_test.elf
BIN     := ddr3_hps_test.bin
SRCS    := startup.S main.c
LD_SCRIPT := linker.ld

# -mcpu=cortex-a9     — target the Cortex-A9 core inside the Cyclone V HPS
# -mfpu=neon-vfpv3    — enable NEON/VFPv3 (matches the A9 hardware)
# -mfloat-abi=hard    — pass float arguments in VFP registers (ABI)
# -ffreestanding      — no C standard library startup assumptions
# -nostdlib           — do not link against libc or libgcc implicitly
# -Os                 — optimise for size
CFLAGS  := -mcpu=cortex-a9 -mfpu=neon-vfpv4 -mfloat-abi=hard \
           -ffreestanding -nostdlib \
           -Os -g -Wall -Wextra

LDFLAGS := -T $(LD_SCRIPT) -nostdlib -Wl,--build-id=none

.PHONY: all clean

all: $(ELF)

$(ELF): $(SRCS) $(LD_SCRIPT)
	$(CC) $(CFLAGS) $(LDFLAGS) $(SRCS) -o $@
	arm-linux-gnueabihf-objcopy -O binary $@ $(BIN)
	arm-linux-gnueabihf-size $@

clean:
	rm -f $(ELF) $(BIN)
