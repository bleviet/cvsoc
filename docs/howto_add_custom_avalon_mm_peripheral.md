# How-to: Create and Integrate a Custom Avalon-MM Slave or Master Peripheral in Platform Designer

> **Series:** cvsoc — Stepping into advanced FPGA development on the DE10-Nano  
> **Type:** How-to Guide (Problem-oriented)  
> **Difficulty:** Intermediate  
> **Applies to:** Phase 2 (`04_nios2_led`, `04b_niosv_led`), Phase 3 (`05_hps_led`), Phase 3B (`15_ddr3_fpga_hps`), Phase 6 (`10_linux_led`)

---

## Overview

Standard Platform Designer IP cores (`altera_avalon_pio`, `altera_avalon_jtag_uart`) are great for simple GPIO, but the real power of an SoC FPGA comes from designing **your own custom VHDL hardware accelerator or peripheral** and exposing its registers to Nios II, Nios V, or the ARM Cortex-A9 over the Avalon Memory-Mapped (Avalon-MM) bus.

This guide shows you step by step how to:
1. Write a clean VHDL-2008 **Avalon-MM Slave** register peripheral.
2. Package it for Platform Designer using a **`_hw.tcl`** component descriptor (like [`15_ddr3_fpga_hps/qsys/ddr3_test_master_hw.tcl`](../15_ddr3_fpga_hps/qsys/ddr3_test_master_hw.tcl)).
3. Instantiate and wire it in your `qsys/*.tcl` script without opening the GUI.
4. Access its registers from C/C++ (bare-metal Nios II/V, bare-metal ARM HPS, or Linux `/dev/mem`).

---

## Step 1 — Write the VHDL Avalon-MM Slave Entity

Create your VHDL file (for example, `hdl/pwm_led_avmm.vhd`). A standard 32-bit Avalon-MM slave with 4 registers (`2`-bit word address `avs_address`) needs only 7 signals:

```vhdl
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity pwm_led_avmm is
  port (
    clk           : in  std_logic;
    reset_n       : in  std_logic;

    -- Avalon-MM Slave Interface (word-addressed: 0x00, 0x04, 0x08, 0x0C)
    avs_address   : in  std_logic_vector(1 downto 0);
    avs_read      : in  std_logic;
    avs_readdata  : out std_logic_vector(31 downto 0);
    avs_write     : in  std_logic;
    avs_writedata : in  std_logic_vector(31 downto 0);

    -- External conduit to physical FPGA pins
    led_out       : out std_logic_vector(7 downto 0)
  );
end entity pwm_led_avmm;

architecture rtl of pwm_led_avmm is
  -- Register 0 (offset 0x00): LED enable bitmask [7:0]
  signal reg_pattern : std_logic_vector(7 downto 0) := x"00";
  -- Register 1 (offset 0x04): PWM duty cycle [7:0] (0=off, 255=full brightness)
  signal reg_duty    : unsigned(7 downto 0) := x"FF";

  signal pwm_cnt     : unsigned(7 downto 0) := (others => '0');
  signal pwm_active  : std_logic;
begin

  -- 1. Slave Write Process
  write_proc : process (clk, reset_n)
  begin
    if reset_n = '0' then
      reg_pattern <= x"00";
      reg_duty    <= x"FF";
    elsif rising_edge(clk) then
      if avs_write = '1' then
        case avs_address is
          when "00"   => reg_pattern <= avs_writedata(7 downto 0);
          when "01"   => reg_duty    <= unsigned(avs_writedata(7 downto 0));
          when others => null;
        end case;
      end if;
    end if;
  end process write_proc;

  -- 2. Slave Read Process (0-cycle combinational read latency)
  read_proc : process (avs_address, reg_pattern, reg_duty)
  begin
    avs_readdata <= (others => '0');
    case avs_address is
      when "00"   => avs_readdata(7 downto 0) <= reg_pattern;
      when "01"   => avs_readdata(7 downto 0) <= std_logic_vector(reg_duty);
      when "10"   => avs_readdata             <= x"CAFE_0001"; -- Hardware ID/Version
      when others => avs_readdata             <= (others => '0');
    end case;
  end process read_proc;

  -- 3. Hardware PWM Engine (runs at 50 MHz / 256 = 195.3 kHz PWM carrier)
  pwm_proc : process (clk, reset_n)
  begin
    if reset_n = '0' then
      pwm_cnt <= (others => '0');
    elsif rising_edge(clk) then
      pwm_cnt <= pwm_cnt + 1;
    end if;
  end process pwm_proc;

  pwm_active <= '1' when pwm_cnt < reg_duty else '0';
  led_out    <= reg_pattern when pwm_active = '1' else x"00";

end architecture rtl;
```

---

## Step 2 — Create the `_hw.tcl` Component Descriptor

Platform Designer discovers custom IP components by scanning the `qsys/` directory (or `$IP_SEARCH_PATH`) for files ending in **`_hw.tcl`**.

Create `qsys/pwm_led_avmm_hw.tcl` (modeled on [`15_ddr3_fpga_hps/qsys/ddr3_test_master_hw.tcl`](../15_ddr3_fpga_hps/qsys/ddr3_test_master_hw.tcl)):

```tcl
package require -exact qsys 16.1

set_module_property NAME pwm_led_avmm
set_module_property VERSION 1.0
set_module_property DISPLAY_NAME "Custom PWM LED Controller (Avalon-MM Slave)"
set_module_property GROUP "Custom IP"
set_module_property INSTANTIATE_IN_SYSTEM_MODULE true

# ── Synthesis fileset ─────────────────────────────────────────────────────────
add_fileset QUARTUS_SYNTH QUARTUS_SYNTH "" ""
set_fileset_property QUARTUS_SYNTH TOP_LEVEL pwm_led_avmm
add_fileset_file pwm_led_avmm.vhd VHDL PATH ../hdl/pwm_led_avmm.vhd TOP_LEVEL_FILE

# ── Clock interface ───────────────────────────────────────────────────────────
add_interface clock clock end
set_interface_property clock ENABLED true
add_interface_port clock clk clk Input 1

# ── Reset interface (active-low reset_n) ──────────────────────────────────────
add_interface reset reset end
set_interface_property reset associatedClock clock
set_interface_property reset synchronousEdges DEASSERT
add_interface_port reset reset_n reset_n Input 1

# ── Avalon-MM Slave interface (avs) ───────────────────────────────────────────
add_interface avs avalon end
set_interface_property avs addressUnits WORDS
set_interface_property avs associatedClock clock
set_interface_property avs associatedReset reset
set_interface_property avs bitsPerSymbol 8
set_interface_property avs readWaitTime 0
set_interface_property avs writeWaitTime 0

add_interface_port avs avs_address   address   Input  2
add_interface_port avs avs_read      read      Input  1
add_interface_port avs avs_readdata  readdata  Output 32
add_interface_port avs avs_write     write     Input  1
add_interface_port avs avs_writedata writedata Input  32

# ── External Conduit (to FPGA top-level pins) ─────────────────────────────────
add_interface conduit_end conduit end
set_interface_property conduit_end associatedClock clock
set_interface_property conduit_end associatedReset reset
add_interface_port conduit_end led_out export Output 8
```

> **Important Note on `addressUnits WORDS`:**
> Setting `addressUnits WORDS` tells Platform Designer that `avs_address[1:0]` indexes **32-bit words** (`0`, `1`, `2`, `3`). From the CPU's perspective (which uses byte addresses), those four registers appear at byte offsets `0x00`, `0x04`, `0x08`, and `0x0C`!

---

## Step 3 — Wire the Component in Your Platform Designer Tcl Script

In your `qsys/hps_system.tcl` (or `nios2_system.tcl` / `niosv_system.tcl`), instantiate `pwm_led_avmm`, connect its clock, reset, and Avalon-MM slave port, and export its conduit:

### Option A: Connecting to the ARM HPS Lightweight Bridge (`hps_system.tcl`)

```tcl
# 1. Instantiate your custom IP
add_instance pwm_led pwm_led_avmm

# 2. Connect the SAME 50 MHz clock used by h2f_lw_axi_clock (avoids clock-crossing FIFO!)
add_connection clk_0.clk pwm_led.clock

# 3. Connect reset from HPS h2f_reset
add_connection hps_0.h2f_reset pwm_led.reset

# 4. Connect to LW H2F AXI master at offset 0x00000020 (ARM address 0xFF200020)
add_connection hps_0.h2f_lw_axi_master pwm_led.avs
set_connection_parameter_value hps_0.h2f_lw_axi_master/pwm_led.avs baseAddress {0x00000020}

# 5. Export the LED conduit to the VHDL top-level entity ( if replacing led_pio )
add_interface pwm_led_external_connection conduit end
set_interface_property pwm_led_external_connection EXPORT_OF pwm_led.conduit_end
```

### Option B: Connecting to Nios II or Nios V (`nios2_system.tcl` / `niosv_system.tcl`)

```tcl
add_instance pwm_led pwm_led_avmm
add_connection clk_0.out_clk          pwm_led.clock
add_connection reset_bridge.out_reset pwm_led.reset

# For Nios II: use nios2.data_master
# For Nios V:  use niosv.data_manager
add_connection niosv.data_manager pwm_led.avs
set_connection_parameter_value niosv.data_manager/pwm_led.avs baseAddress {0x00010040}
```

Ensure `qsys-script` and `qsys-generate` know to search `../qsys` for `_hw.tcl` files by passing `--search-path=../qsys,$` (already done in `15_ddr3_fpga_hps/quartus/Makefile`):

```bash
qsys-script --search-path=../qsys,$ --script=../qsys/hps_system.tcl
qsys-generate hps_system.qsys --search-path=../qsys,$ --synthesis=VHDL
```

---

## Step 4 — Access Your Custom Registers from Software

### From Bare-Metal ARM HPS (`0xFF200000 + 0x20`)

```c
#define PWM_LED_BASE    0xFF200020UL
#define PWM_REG_PATTERN (*(volatile uint32_t *)(PWM_LED_BASE + 0x00UL))
#define PWM_REG_DUTY    (*(volatile uint32_t *)(PWM_LED_BASE + 0x04UL))
#define PWM_REG_ID      (*(volatile uint32_t *)(PWM_LED_BASE + 0x08UL))

/* Verify hardware ID reads 0xCAFE0001 */
uint32_t hw_id = PWM_REG_ID;

/* Set LEDs to 0xAA at 25% brightness (64 / 255) */
PWM_REG_DUTY    = 64;
PWM_REG_PATTERN = 0xAA;
```

### From Embedded Linux User Space (`/dev/mem`)

Because `mmap()` requires a page-aligned base (`0xFF200000`, multiple of `4096`), map the base of the LW bridge and add the word offset (`0x20 / 4 = 8`):

```c
int fd = open("/dev/mem", O_RDWR | O_SYNC);
volatile uint32_t *lw_base = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
                                  MAP_SHARED, fd, 0xFF200000);
volatile uint32_t *pwm_regs = lw_base + (0x20 / sizeof(uint32_t));

pwm_regs[1] = 64;    /* Offset 0x04: Duty cycle */
pwm_regs[0] = 0xAA;  /* Offset 0x00: Pattern    */
```

### From Nios II / Nios V (`system.h`)

After running `make bsp`, `system.h` automatically defines `#define PWM_LED_BASE 0x10040`:

```c
#include "system.h"
#include "io.h"

IOWR_32DIRECT(PWM_LED_BASE, 0x04, 64);    /* Duty cycle */
IOWR_32DIRECT(PWM_LED_BASE, 0x00, 0xAA);  /* Pattern    */
```

---

## Need an Avalon-MM Master Instead of a Slave?

If your VHDL component needs to initiate reads/writes to DDR3 via the `f2h_sdram0` bridge, inspect the complete working reference in this repository:
- **VHDL Master + Slave RTL:** [`15_ddr3_fpga_hps/hdl/ddr3_test_master.vhd`](../15_ddr3_fpga_hps/hdl/ddr3_test_master.vhd)
- **Platform Designer `_hw.tcl`:** [`15_ddr3_fpga_hps/qsys/ddr3_test_master_hw.tcl`](../15_ddr3_fpga_hps/qsys/ddr3_test_master_hw.tcl)
- **Deep-Dive Explanation:** [`docs/explanation_phase3b_ddr3_and_avalon_master.md`](explanation_phase3b_ddr3_and_avalon_master.md)
