# nios2_debug.gdb — GDB initialisation script for Nios II debugging.
#
# Used by: make gdb  (in 08_nios2_debug/quartus/)
# Requires: nios2-gdb-server running on localhost:2345 (start with 'make gdb-server')
#
# This script demonstrates five GDB debugging techniques for embedded Nios II:
#   1. Connecting to nios2-gdb-server
#   2. Loading the ELF with symbols
#   3. Setting a hardware breakpoint on set_led()
#   4. Setting a watchpoint on debug_state.step_count
#   5. Defining a helper command to inspect Avalon peripheral registers
#
# Usage:
#   Terminal 1:  make gdb-server          (starts nios2-gdb-server on port 2345)
#   Terminal 2:  make gdb                 (runs this script, opens GDB prompt)

# ── 1. Connect to nios2-gdb-server ───────────────────────────────────────────
target remote localhost:2345

# ── 2. Load ELF sections into Nios II on-chip RAM ────────────────────────────
# 'load' writes .text, .data, and .rodata sections.
# After load the CPU is halted at address 0x00000000 (reset vector).
load

# ── 3. Software breakpoint on set_led ────────────────────────────────────────
# A software breakpoint replaces the instruction at set_led() with a BKPT.
# This works because the ELF is loaded to on-chip RAM (writable).
# Use 'hbreak' only when code is in ROM/Flash where writing is not possible.
# The Nios II Tiny core has 2 hardware breakpoint triggers; keeping one free
# allows a hardware watchpoint (step 4) to be armed simultaneously.
break set_led

# ── 4. Demonstrate watchpoint with a second breakpoint ───────────────────────
# The Nios II Tiny core has only 1 hardware trigger (used for hardware
# breakpoints only; hardware watchpoints are not supported).
#
# Alternative: set a software breakpoint at the line where step_count is reset.
# Press KEY[1] during a GDB session to reach this line:
#   break process_button
# Then when halted inside process_button, inspect debug_state.step_count.
#
# For platforms with watchpoint hardware (e.g. Cortex-A9 in 09_hps_debug),
# use: watch debug_state.step_count

# ── 5. Helper commands for Avalon peripheral inspection ──────────────────────

# inspect-led-pio: show the LED PIO data register (BASE + offset 0).
define inspect-led-pio
  echo === LED PIO DATA register (0x00010010) ===\n
  x/1xw 0x10010
end

# inspect-button-pio: show all button PIO registers.
define inspect-button-pio
  echo === Button PIO registers (0x00010020) ===\n
  echo DATA      (0x00010020): \n
  x/1xw 0x10020
  echo DIRECTION (0x00010024): \n
  x/1xw 0x10024
  echo IRQ_MASK  (0x00010028): \n
  x/1xw 0x10028
  echo EDGE_CAP  (0x0001002C): \n
  x/1xw 0x1002C
end

# inspect-debug-state: print the full debug_state struct.
define inspect-debug-state
  print debug_state
end

# ── Start execution ───────────────────────────────────────────────────────────
# The CPU runs until it hits the breakpoint at set_led() on the first
# call from main().
echo \n=== Nios II GDB session started ===\n
echo Breakpoint set at set_led() — execution will halt there.\n
echo Commands available:\n
echo   inspect-led-pio       — read LED PIO DATA register\n
echo   inspect-button-pio    — read all button PIO registers\n
echo   inspect-debug-state   — print debug_state struct\n
echo   print debug_state     — same as above\n
echo   x/20ub &patterns      — dump the pattern table\n
echo   bt                    — backtrace (shows call stack)\n
echo   continue              — resume execution\n
echo   step / next / finish  — single-step\n
echo \n

continue
