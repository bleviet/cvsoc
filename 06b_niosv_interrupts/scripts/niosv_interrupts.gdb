# niosv_interrupts.gdb — RISC-V GDB init script for 06b_niosv_interrupts.
#
# Usage:
#   Terminal 1:  make gdb-server        (starts ash-riscv-gdb-server on port 2345)
#   Terminal 2:  make gdb-tui           (runs this script, opens RISC-V GDB TUI)
#
# Requires the RiscFree RISC-V toolchain (riscv32-unknown-elf-gdb +
# ash-riscv-gdb-server).  What to expect:
#   The ELF is loaded, a breakpoint is placed at button_isr(), and execution
#   starts.  Press KEY[1] or KEY[0] on the DE10-Nano to trigger the interrupt
#   and land inside button_isr().

# ── 1. Connect to ash-riscv-gdb-server ────────────────────────────────────────
set non-stop off
set arch riscv:rv32
target extended-remote localhost:2345

# ── 2. Load ELF into Nios V on-chip RAM (32 KB at address 0x00000000) ────────
load

# ── 3. Breakpoint at the interrupt service routine ────────────────────────────
# button_isr() is called by the HAL whenever the button PIO asserts its IRQ.
# Press KEY[1] or KEY[0] on the board to hit this breakpoint.
break button_isr

# ── 4. Helper commands for peripheral inspection ─────────────────────────────

# inspect-led: show the 8-bit LED PIO output register.
define inspect-led
  echo === LED PIO DATA (0x00010010) ===\n
  x/1xw 0x10010
end

# inspect-buttons: show the button PIO data and edge-capture registers.
define inspect-buttons
  echo === Button PIO DATA      (0x00010020) ===\n
  x/1xw 0x10020
  echo === Button PIO EDGE_CAP  (0x0001002C) ===\n
  x/1xw 0x1002C
end

# ── 5. Start execution ────────────────────────────────────────────────────────
echo \n=== 06b_niosv_interrupts RISC-V GDB session ready ===\n
echo Press KEY[0] or KEY[1] on the board to trigger button_isr().\n
echo \n
echo Available commands:\n
echo   inspect-led       — read LED PIO output register\n
echo   inspect-buttons   — read button PIO data + edge-capture\n
echo   print $a0         — print a RISC-V register (arg0)\n
echo   bt                — backtrace\n
echo   continue          — resume until next interrupt\n
echo   step / next       — single-step\n
echo \n

continue
