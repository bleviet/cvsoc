# ARM bare-metal application Makefile for DDR3 FPGA+HPS shared access test.
#
# Cross-compiles main.c + startup.S into a flat binary that runs from
# DDR3 SDRAM at 0x00100000 on the DE10-Nano Cyclone V SoC.

CC      := arm-linux-gnueabihf-gcc

ELF     := ddr3_fpga_hps.elf
BIN     := ddr3_fpga_hps.bin
SRCS    := startup.S main.c
LD_SCRIPT := linker.ld

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
