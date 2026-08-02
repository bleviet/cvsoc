# niosv_debug.gdb — RISC-V GDB initialisation script for Nios V debugging.
#
# Used by: make gdb  (in 08b_niosv_debug/quartus/)
# Requires: ash-riscv-gdb-server running on localhost:2345 (start with 'make gdb-server')
# Requires the RiscFree RISC-V toolchain (riscv32-unknown-elf-gdb).
#
# This script demonstrates five GDB debugging techniques for embedded Nios V:
#   1. Connecting to the Ashling RiscFree GDB server
#   2. Loading the ELF with symbols
#   3. Setting a breakpoint on set_led()
#   4. Setting a watchpoint on debug_state.step_count
#   5. Defining a helper command to inspect Avalon peripheral registers
#
# Usage:
#   Terminal 1:  make gdb-server          (starts ash-riscv-gdb-server on port 2345)
#   Terminal 2:  make gdb                 (runs this script, opens GDB prompt)

# ── 1. Connect to ash-riscv-gdb-server ────────────────────────────────────────
set non-stop off
set arch riscv:rv32
target extended-remote localhost:2345

# ── 2. Load ELF sections into Nios V on-chip RAM ──────────────────────────────
# 'load' writes .text, .data, and .rodata sections.
load

# ── 3. Breakpoint on set_led ──────────────────────────────────────────────────
# set_led() is called from the main loop on every pattern advance.
break set_led

# ── 4. Watchpoint on step_count (RISC-V debug hardware permitting) ────────────
# Press KEY[1] to reset step_count to 0.  If the connected probe/core lacks
# watchpoint hardware, fall back to 'break process_button'.
# watch debug_state.step_count

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
echo \n=== Nios V RISC-V GDB session started ===\n
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
