# ARM bare-metal HPS debug application Makefile.
#
# Cross-compiles startup.S + main.c into a flat binary targeting
# HPS OCRAM (0xFFFF0000).  Compiled with -O0 -g3 for maximum GDB visibility.

CC        := arm-linux-gnueabihf-gcc

ELF       := hps_debug.elf
SRCS      := startup.S main.c
LD_SCRIPT := linker.ld

# -O0 -g3: disable optimisation, emit maximum debug info.
# This ensures all variables are live in memory (no register-only variables)
# and all function calls appear in the backtrace.
CFLAGS  := -mcpu=cortex-a9 -mfpu=neon-vfpv4 -mfloat-abi=hard \
           -ffreestanding -nostdlib \
           -O0 -g3 -Wall -Wextra

LDFLAGS := -T $(LD_SCRIPT) -nostdlib -Wl,--build-id=none

.PHONY: all clean

all: $(ELF)

$(ELF): $(SRCS) $(LD_SCRIPT)
	$(CC) $(CFLAGS) $(LDFLAGS) $(SRCS) -o $@
	arm-linux-gnueabihf-objcopy -O binary $@ hps_debug.bin
	arm-linux-gnueabihf-size $@

clean:
	rm -f $(ELF) hps_debug.bin
