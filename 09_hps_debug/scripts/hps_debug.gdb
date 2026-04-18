# hps_debug.gdb — GDB initialisation script for ARM HPS debugging.
#
# Used by: make gdb  (in 09_hps_debug/quartus/)
# Requires: OpenOCD running (start with 'make openocd')
#
# GDB client: arm-none-eabi-gdb (installed by 'make setup')
#
# This script demonstrates GDB debugging techniques for bare-metal ARM:
#   1. Connecting to OpenOCD GDB server on port 3333
#   2. Loading the ELF into OCRAM (sets both binary and program counter)
#   3. Hardware breakpoint at main() entry
#   4. Watchpoint on g_debug.led_pattern
#   5. Helper commands to inspect GIC and Avalon peripheral registers
#
# Usage:
#   Terminal 1:  make openocd          (starts OpenOCD, halts CPU)
#   Terminal 2:  make gdb              (runs this script)

# ── 1. Connect to OpenOCD GDB server ─────────────────────────────────────────
target remote localhost:3333

# Disable MMU/caches so raw memory reads reflect actual register values.
# CP15 Control Register (c1, c0, 0): clear bit 0 (MMU) and bit 2 (D-cache).
monitor arm mcr 15 0 1 0 0 0

# ── 2. Load ELF into HPS OCRAM ───────────────────────────────────────────────
# 'load' writes .text, .data, and .rodata sections to OCRAM at 0xFFFF0000
# AND sets the PC to the ELF entry point (0xFFFF0000 = start of startup.S).
# This is the correct single-step to both upload code and set the program counter.
load

# ── 3. Hardware breakpoint at main() ─────────────────────────────────────────
# Halt right at the entry to main() to step through initialization.
# Use 'si' (stepi) to trace through startup.S → main() call.
hbreak main

# ── 4. Watchpoint on g_debug.led_pattern ─────────────────────────────────────
# Fires whenever the LED pattern is updated (by main loop OR by ISR).
watch g_debug.led_pattern

# ── 5. Helper commands ────────────────────────────────────────────────────────

# inspect-gicd: show GIC Distributor enable registers and button IRQ config.
define inspect-gicd
  echo === GIC Distributor (0xFFFED000) ===\n
  echo GICD_CTLR       (0xFFFED000): \n
  x/1xw 0xFFFED000
  echo GICD_ISENABLER1 (0xFFFED104): \n
  x/1xw 0xFFFED104
  echo GICD_IPRIORITYR (0xFFFED448, ID 72): \n
  x/1xb 0xFFFED448
  echo GICD_ITARGETSR  (0xFFFED848, ID 72): \n
  x/1xb 0xFFFED848
end

# inspect-gicc: show GIC CPU Interface status.
define inspect-gicc
  echo === GIC CPU Interface (0xFFFEC100) ===\n
  echo GICC_CTLR (0xFFFEC100): \n
  x/1xw 0xFFFEC100
  echo GICC_PMR  (0xFFFEC104): \n
  x/1xw 0xFFFEC104
  echo GICC_IAR  (0xFFFEC10C): \n
  x/1xw 0xFFFEC10C
end

# inspect-button-pio: show all button PIO registers via ARM address.
define inspect-button-pio
  echo === Button PIO (LW H2F 0xFF201000) ===\n
  echo DATA      (0xFF201000): \n
  x/1xw 0xFF201000
  echo IRQ_MASK  (0xFF201008): \n
  x/1xw 0xFF201008
  echo EDGE_CAP  (0xFF20100C): \n
  x/1xw 0xFF20100C
end

# inspect-led-pio: show LED PIO data register.
define inspect-led-pio
  echo === LED PIO DATA (0xFF200000) ===\n
  x/1xw 0xFF200000
end

# inspect-debug: print the g_debug struct.
define inspect-debug
  print g_debug
end

# ── Start execution ───────────────────────────────────────────────────────────
echo \n=== HPS GDB session started ===\n
echo Hardware breakpoint set at main().\n
echo Commands available:\n
echo   inspect-gicd          — read GIC Distributor registers\n
echo   inspect-gicc          — read GIC CPU Interface registers\n
echo   inspect-button-pio    — read button PIO registers\n
echo   inspect-led-pio       — read LED PIO data register\n
echo   inspect-debug         — print g_debug struct\n
echo   si / stepi            — single instruction step (assembly level)\n
echo   hbreak irq_c_handler  — break on next button press\n
echo   bt                    — backtrace\n
echo   continue              — resume execution\n
echo \n

continue
