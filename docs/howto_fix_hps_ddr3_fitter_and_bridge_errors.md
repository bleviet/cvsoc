# How-to: Fix HPS DDR3 Fitter Error 174068, Bridge Data Aborts, and Watchdog Resets

> **Series:** cvsoc — Stepping into advanced FPGA development on the DE10-Nano  
> **Type:** How-to Guide (Problem-oriented)  
> **Difficulty:** Intermediate–Advanced  
> **Applies to:** All ARM HPS Projects (`05_hps_led`, `07_hps_interrupts`, `09_hps_debug`, `10_linux_led`, `11_ethernet_hps_led`, `12_zephyr_led`, `14_ddr3_hps_test`, `15_ddr3_fpga_hps`, `16_field_update`)

---

## Overview

When building or running a Cyclone V ARM Hard Processor System (HPS) project, five hardware/toolchain errors account for 95% of bring-up failures. This guide provides immediate, copy-pasteable recipes to diagnose and fix each one.

---

## Problem 1: Quartus Fitter Fails with `Error (174068): Output buffer atom ... does not use calibrated on-chip termination`

### Symptom
During `quartus_fit` on an HPS project, compilation fails with dozens of errors like:
```text
Error (174068): Output buffer atom "hps_system:u0|...:obuf_os_0" has port "SERIESTERMINATIONCONTROL[0]" connected, but does not use calibrated on-chip termination
```

### Root Cause
1. `qsys-generate` emits `altdq_dqs2_acv_connect_to_hard_phy_cyclonev.sv` without `.use_termination_control("true")` on five `cyclonev_io_obuf` primitives.
2. Or your `.qsf` / `de10_nano_project.tcl` includes `QSYS_FILE` instead of `QIP_FILE`, which causes Quartus to re-generate unpatched IP inside `db/ip/` and ignore `patch_oct.py`.
3. Or the two-pass DDR3 pin assignment script (`hps_sdram_p0_pin_assignments.tcl`) was not applied after `quartus_map`.

### Fix Recipe

1. **In `hps_system.tcl`**, ensure `MEM_RTT_NOM` is set on `hps_0`:
   ```tcl
   set_instance_parameter_value hps_0 MEM_RTT_NOM {RZQ/6}
   ```
2. **In `de10_nano_project.tcl`**, reference **only the `.qip` file**, never the `.qsys` file:
   ```tcl
   # CORRECT:
   set_global_assignment -name QIP_FILE hps_system/synthesis/hps_system.qip

   # NEVER USE THIS ON HPS PROJECTS (overwrites patch_oct.py fixes):
   # set_global_assignment -name QSYS_FILE ../qsys/hps_system.qsys
   ```
3. **Run `patch_oct.py` and the two-pass compile** in your `quartus/` directory:
   ```bash
   python3 ../scripts/patch_oct.py \
     hps_system/synthesis/submodules/altdq_dqs2_acv_connect_to_hard_phy_cyclonev.sv
   ```
   Then run Pass 1 (`quartus_map`), apply the generated DDR3 pin Tcl script (`quartus_sta -t .../hps_sdram_p0_pin_assignments.tcl de10_nano`), and run Pass 2 (`quartus_sh --flow compile de10_nano -c de10_nano`)—all automated by `make compile` in `05_hps_led/quartus/Makefile`.

---

## Problem 2: `qsys-generate` Fails with `EntityWritingException: Failed to write entity for hps_system_hps_0_hps_io_border`

### Symptom
Running `qsys-generate` on an HPS system crashes with a Java stack trace ending in `EntityWritingException` and produces zero VHDL/Verilog files.

### Root Cause
`hps_0.h2f_lw_axi_clock` (or `hps_0.f2h_sdram0_clock`) is either unconnected or connected to a different `clock_source` instance than the Avalon-MM peripherals connected to that bridge.

### Fix Recipe
In `qsys/hps_system.tcl`, drive the HPS bridge clock inputs from the **exact same `clk_0.clk` output** that clocks your FPGA peripherals:

```tcl
add_instance clk_0 clock_source
set_instance_parameter_value clk_0 clockFrequency {50000000}

# Connect clk_0.clk to BOTH the peripheral and the HPS bridge clock ports:
add_connection clk_0.clk led_pio.clk
add_connection clk_0.clk hps_0.h2f_lw_axi_clock
add_connection clk_0.clk hps_0.f2h_sdram0_clock
```

---

## Problem 3: ARM CPU Hits a `Data Abort` (or Linux `Bus error` / Hang) When Accessing `0xFF200000`

### Symptom
- **In Bare-Metal / GDB (`05`, `07`, `09`):** Execution jumps to the Data Abort vector (`0xFFFF0010`) or resets back to `_start` the instant `LED_PIO_DATA = ...` (`0xFF200000`) executes.
- **In Embedded Linux (`10`, `11`, `16`):** Running `devmem 0xFF200000` or starting `fpga_led` / `led_server` prints `Bus error` or freezes the terminal.

### Root Cause
One of three things is missing:
1. The FPGA fabric is not programmed (`CONF_DONE` LED on the DE10-Nano is off).
2. The Lightweight H2F bridge is still held in reset in `RSTMGR_BRGMODRST` (`0xFFD0501C`).
3. The Lightweight H2F address window is not mapped into the L3 interconnect (`L3_REMAP` at `0xFF800000` bit 4 is `0`).

### Fix Recipe (Bare-Metal C)

Always run this initialization sequence before touching any address in `0xFF200000`–`0xFF3FFFFF`:

```c
#define RSTMGR_BRGMODRST (*(volatile uint32_t *)0xFFD0501CUL)
#define L3_REMAP         (*(volatile uint32_t *)0xFF800000UL)

static void hps_bridge_init(void)
{
    /* 1. Make LW H2F (bit 4) and OCRAM at 0x0 (bit 0) visible in L3 NIC-301 */
    L3_REMAP = (1u << 4) | (1u << 0);

    /* 2. Pulse bridge resets (assert -> delay -> release) */
    RSTMGR_BRGMODRST |= 0x7u;
    for (volatile int i = 0; i < 200000; i++) ;
    RSTMGR_BRGMODRST &= ~0x7u;
    for (volatile int i = 0; i < 200000; i++) ;
}
```

### Fix Recipe (Embedded Linux Target Shell)

On the DE10-Nano serial/SSH console:
```bash
# 1. Check if fpga_load kernel module succeeded at boot:
dmesg | grep fpga_load
# Expected: "fpga_load: FPGA programmed successfully!" and "LW H2F bridge enabled"

# 2. If not loaded, load it via the init script:
/etc/init.d/S30fpga_load start

# 3. Or manually release bridge reset and enable L3 remap with devmem:
devmem 0xFFD0501C 32 0x00000000
devmem 0xFF800000 32 0x00000019
```

---

## Problem 4: Bare-Metal HPS Program Resets or Stops Running After ~5–30 Seconds

### Symptom
Your bare-metal HPS program (`05`, `07`, `14`, `15`) starts running and animates the LEDs, but after a few seconds the board resets or halts.

### Root Cause
The Cyclone V's **L4 Watchdog 0 (`0xFFD02000`)** has `WDT_ALWAYS_EN=1` in silicon: once enabled by the BootROM or U-Boot SPL, it **cannot be disabled by software**—it must be set to maximum timeout (`TORR = 0xFF`) and periodically kicked (`CRR = 0x76`), while the **ARM MPCore Private Watchdog (`0xFFFEC620`)** must be unlocked and disabled.

### Fix Recipe

Add `wdt_init()` at the start of `main()` and call `wdt_kick()` inside your main loop and delay routines (see [`05_hps_led/software/app/main.c`](../05_hps_led/software/app/main.c)):

```c
#define WDT0_BASE          0xFFD02000UL
#define WDT1_BASE          0xFFD03000UL
#define WDT_TORR(base)     (*(volatile uint32_t *)((base) + 0x04UL))
#define WDT_CRR(base)      (*(volatile uint32_t *)((base) + 0x0CUL))
#define MPCORE_WDT_CTRL    (*(volatile uint32_t *)0xFFFEC628UL)
#define MPCORE_WDT_DISABLE (*(volatile uint32_t *)0xFFFEC634UL)

static void wdt_init(void)
{
    WDT_TORR(WDT0_BASE) = 0xFFu;
    WDT_CRR(WDT0_BASE)  = 0x76u;
    WDT_TORR(WDT1_BASE) = 0xFFu;
    WDT_CRR(WDT1_BASE)  = 0x76u;

    MPCORE_WDT_DISABLE = 0x12345678u;
    MPCORE_WDT_DISABLE = 0x87654321u;
    MPCORE_WDT_CTRL    = 0;
}
```

---

## Problem 5: `Data Abort` When Loading an ELF to DDR3 (`0x00100000`) via OpenOCD (`14`, `15`, `12`)

### Symptom
In `14_ddr3_hps_test`, `15_ddr3_fpga_hps`, or `12_zephyr_led`, OpenOCD fails during `load_image` to `0x00100000` with `Data Abort`.

### Root Cause
Unlike OCRAM (`0xFFFF0000`), DDR3 (`0x00100000`) is uninitialized after power-on until **U-Boot SPL** calibrates the DDR3 PHY and releases the SDRAM controller reset.

### Fix Recipe
Either:
1. Leave the Phase 6/7 Linux microSD card inserted in the DE10-Nano when powering on the board (so the SD card's Partition 1 `u-boot-spl` initializes DDR3 automatically at power-up before OpenOCD halts the CPU), **or**
2. Configure your OpenOCD script to load `u-boot-spl` into `0xFFFF0000` and run it for 1 second before loading your DDR3 ELF (as done in `12_zephyr_led`).

---

## Related Documentation

- **Explanations:**
  - [Explanation: Cyclone V HPS Silicon Architecture, Bridges, Watchdogs, and the DDR3 PHY Compile Flow](explanation_phase3_hps_baremetal_architecture.md)
  - [Explanation: Cyclone V DDR3 Subsystem, F2H SDRAM Bridge, and Custom Avalon-MM Masters](explanation_phase3b_ddr3_and_avalon_master.md)
