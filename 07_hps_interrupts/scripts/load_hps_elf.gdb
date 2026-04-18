# GDB script to interactively debug the HPS bare-metal ELF on the DE10-Nano.
# NOTE: For simple load+run use `make download-elf` (OpenOCD-only, no GDB needed).
#
# Usage (from 07_hps_interrupts directory, after `openocd -f scripts/de10_nano_hps.cfg`):
#   gdb-multiarch -x scripts/load_hps_elf.gdb software/app/hps_interrupts.elf

# Connect to OpenOCD GDB server
target remote localhost:3333

# Halt the core before loading
monitor halt

# Disable MMU/caches so raw memory writes reach OCRAM
monitor arm mcr 15 0 1 0 0 0

# Load ELF sections into target memory (OCRAM at 0xFFFF0000)
load

# Jump to entry point and let it run
continue
