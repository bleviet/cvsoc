# Phase 3 Tutorial — HPS Bare-Metal LED Control

> **Series:** cvsoc — Stepping into advanced FPGA development on the DE10-Nano  
> **Phase:** 3 of 6  
> **Difficulty:** Intermediate-Advanced — you have completed phases 0–2 and are comfortable with Platform Designer and the Quartus compilation flow

---

## What you will build

By the end of this tutorial you will have a complete HPS bare-metal embedded system on your DE10-Nano:

- The **ARM Cortex-A9 Hard Processor System (HPS)** executing C firmware from On-Chip RAM
- **DDR3 SDRAM** initialised by the HPS memory controller (required by the silicon, even if your application does not use it)
- An **8-bit LED PIO** peripheral in the FPGA fabric
- The **Lightweight HPS-to-FPGA AXI bridge** connecting the ARM address space to the PIO
- An LED pattern that cycles autonomously, driven from ARM C code

Every step is driven from the command line inside a Docker container. No GUI required.

```
                 ┌────────────────────────────────────────────────────────┐
                 │                hps_system (Platform Designer)           │
                 │                                                         │
FPGA_CLK1_50 ──►│─ clk_0 (50 MHz) ──────────────────────┐                │
                 │                                        │                │
                 │  ┌──────────────────────┐              ▼                │
                 │  │  HPS (ARM Cortex-A9) │◄──── h2f_lw_axi_clock        │
                 │  │                      │                               │
                 │  │  OCRAM  0xFFFF0000   │   LW H2F AXI bridge          │
                 │  │  (64 KB, code+stack) │   base: 0xFF200000           │
                 │  │                      │──────────────────────────────►│
                 │  │  SYSMGR  0xFFD08000  │                LED PIO slave  │
                 │  │  RSTMGR  0xFFD05000  │                0xFF200000    ──► LED[7:0]
                 │  └──────────────────────┘                               │
                 │                                                         │
                 │  DDR3 controller ──────────────────────────────────────►│── memory_mem_*
                 └────────────────────────────────────────────────────────┘
                                                                      (HPS hard pins)
```

---

## Prerequisites

| Requirement | Details |
|---|---|
| **Docker** | `raetro/quartus:23.1` image available locally |
| **Repository** | `git clone` of `bleviet/cvsoc`; phases 0 and 1 already working |
| **Phase 2** | Phase 2 completed or understood; you know what Platform Designer and `qsys-generate` do |
| **Board** | Terasic DE10-Nano (Cyclone V `5CSEBA6U23I7`) |
| **JTAG cable** | USB-Blaster (built in on the DE10-Nano) |

Verify the Docker image is present:

```bash
docker images | grep raetro/quartus
# Expected: raetro/quartus   23.1   ...
```

---

## Concepts in 5 minutes

Before touching any file, read these four ideas. Each one affects a concrete decision you will make in this project.

### HPS: a real CPU, not a soft-core

Phase 2 used a **Nios II soft-core** — a processor synthesised into FPGA logic cells from HDL. The Cyclone V SoC also contains a **Hard Processor System (HPS)**: a dual-core ARM Cortex-A9 fabricated in silicon on the same die as the FPGA fabric. It is not synthesised; it is always present and cannot be changed.

The consequence is that the HPS has its own dedicated hard logic for DDR3 SDRAM, Ethernet, USB, I2C, UART, and more. These peripherals appear at fixed addresses in the ARM memory map that you cannot move.

### The HPS always needs DDR3

The HPS memory controller is part of the silicon. Quartus and Platform Designer require you to fully configure it — supplying the exact DDR3 speed grade, geometry, and timing parameters for the physical chips on your board — even if your application never reads or writes a byte of DDR3.

If you omit the DDR3 configuration, `qsys-generate` fails. If you configure it incorrectly (wrong parameters, wrong I/O standards, missing OCT calibration), the **Fitter** fails with errors in the generated DDR3 PHY soft model. Section 2 of this tutorial explains how to handle one such Quartus-specific deficiency in the generated code.

### The Lightweight HPS-to-FPGA bridge

The Cyclone V SoC provides several AXI bridges between the ARM HPS and the FPGA fabric. This project uses the **Lightweight HPS-to-FPGA (LW H2F)** bridge:

- It exposes a **2 MB window** of the FPGA fabric address space to the ARM
- The window base address is fixed at `0xFF200000` in the ARM memory map
- It is designed for **low-bandwidth, low-latency register access** — exactly what an LED PIO needs
- Before the bridge can be used, the HPS firmware must enable it through two registers in the **System Manager** and **Reset Manager**

Any peripheral you place on the LW H2F bridge in Platform Designer becomes accessible from ARM C code as a simple memory-mapped register at `0xFF200000 + <base_offset>`.

### ARM bare-metal: no BSP, no JTAG CPU

Phase 2 used the Nios II **HAL (Hardware Abstraction Layer)** — a generated BSP that provided `system.h`, linker scripts, and startup code. The ARM HPS has no equivalent generated BSP in this project.

Instead, you write three small files by hand:

| File | Purpose |
|---|---|
| `startup.S` | Assembly stub: sets CPU mode, initialises the stack, zeroes `.bss`, calls `main()` |
| `linker.ld` | Places `.startup`, `.text`, `.data`, `.bss`, and the stack in HPS On-Chip RAM (OCRAM) |
| `main.c` | Your application; reads and writes peripheral registers directly using `volatile` pointers |

The reason for running from **OCRAM** (`0xFFFF0000`, 64 KB) rather than DDR3 is that OCRAM requires no memory controller initialisation — the code is ready to execute the instant JTAG transfers it. This is ideal for development iteration.

---

## Project layout

```
05_hps_led/
├── .gitignore
├── qsys/
│   └── hps_system.tcl          ← Platform Designer system (source of truth)
├── scripts/
│   └── patch_oct.py            ← fixes a deficiency in the generated DDR3 PHY
├── quartus/
│   ├── Makefile                ← full build orchestrator
│   ├── de10_nano_project.tcl   ← Quartus project creation
│   ├── de10_nano_pin_assignments.tcl
│   └── de10_nano.sdc
└── software/
    └── app/
        ├── Makefile
        ├── startup.S           ← ARM Cortex-A9 reset entry point
        ├── linker.ld           ← OCRAM memory map
        └── main.c              ← LED cycling application
```

The `hdl/` directory contains the VHDL top-level wrapper (`de10_nano_top.vhd`) that connects board-level I/O pins (`fpga_clk1_50`, `key[0]`, `led[7:0]`, `hps_ddr3_*`) to the generated Platform Designer system (`hps_system`). This wrapper is the Quartus top-level entity.

---

## Step 1 — Design the Platform Designer system

The entire system is defined in `qsys/hps_system.tcl`. This is the authoritative source of truth; the `.qsys` file and all generated HDL are regenerated from it at build time and are not committed to the repository.

### 1.1 Script structure

Every `qsys-script` file begins with:

```tcl
package require -exact qsys 12.0
```

Then names the system and targets the device:

```tcl
create_system hps_system
set_project_property DEVICE_FAMILY {Cyclone V}
set_project_property DEVICE {5CSEBA6U23I7}
```

The rest of the script adds components, configures them, wires them together, and exports the top-level ports. The pattern is identical to Phase 2; the new element is the `altera_hps` component.

### 1.2 The HPS component and DDR3 configuration

The `altera_hps` component represents the entire ARM HPS, including its DDR3 memory controller. Adding and configuring it requires specifying the DDR3 chip parameters exactly:

```tcl
add_instance hps_0 altera_hps

# DDR3 protocol
set_instance_parameter_value hps_0 HPS_PROTOCOL     {DDR3}
set_instance_parameter_value hps_0 MEM_VENDOR       {Micron}
set_instance_parameter_value hps_0 MEM_FORMAT       {DISCRETE}

# DDR3-800: two MT41K256M16HA chips give 32-bit bus at 400 MHz (= 800 MT/s ÷ 2)
set_instance_parameter_value hps_0 MEM_CLK_FREQ     {400.0}
set_instance_parameter_value hps_0 MEM_DQ_WIDTH     {32}
set_instance_parameter_value hps_0 MEM_DQ_PER_DQS   {8}
set_instance_parameter_value hps_0 MEM_ROW_ADDR_WIDTH {15}
set_instance_parameter_value hps_0 MEM_COL_ADDR_WIDTH {10}
set_instance_parameter_value hps_0 MEM_BANKADDR_WIDTH {3}
set_instance_parameter_value hps_0 MEM_CK_WIDTH     {1}
```

The timing parameters (`TIMING_TIS`, `TIMING_TIH`, etc.) are taken from the Micron MT41K256M16HA-107 datasheet. The full list is in `hps_system.tcl`.

**One critical parameter:** `MEM_RTT_NOM` must be set:

```tcl
set_instance_parameter_value hps_0 MEM_RTT_NOM      {RZQ/6}
```

This enables the HPS On-Chip Termination (OCT) calibration circuitry. Without it — or with the wrong value — the generated DDR3 PHY soft model will contain a bug that the Quartus Fitter exposes as Error 174068. See Step 2 for the full explanation.

Enable only the bridge you need:

```tcl
# Enable the Lightweight HPS-to-FPGA bridge
set_instance_parameter_value hps_0 LWH2F_Enable  {true}

# Disable the full HPS-to-FPGA and FPGA-to-HPS heavy bridges (not needed here)
set_instance_parameter_value hps_0 F2S_Width     {0}
set_instance_parameter_value hps_0 S2F_Width     {0}
```

### 1.3 The clock domain rule

The LED PIO and the HPS LW bridge must be in the **same clock domain**. If they are in different domains, Platform Designer inserts clock-crossing FIFO logic — and for the Cyclone V hard HPS component, that insertion triggers an `EntityWritingException` that causes `qsys-generate` to produce zero output files.

The fix is to drive the HPS LW bridge clock from the same clock source as the LED PIO:

```tcl
add_instance clk_0 clock_source
set_instance_parameter_value clk_0 clockFrequency {50000000}

add_instance led_pio altera_avalon_pio
# ...

# Both led_pio and the HPS LW bridge share the same 50 MHz clock
add_connection clk_0.clk led_pio.clk
add_connection clk_0.clk hps_0.h2f_lw_axi_clock   # ← same domain, no crossing needed
add_connection clk_0.clk hps_0.f2h_sdram0_clock    # ← required by HPS even if F2H SDRAM unused
```

> **Key learning #1 — Drive `h2f_lw_axi_clock` from `clk_0` internally.**  
> The `h2f_lw_axi_clock` port of `altera_hps` must receive a clock from the Platform Designer system — it cannot be left undriven. If you wire it from a *separate* clock source, Platform Designer detects a clock domain crossing between the bridge master and `led_pio.s1` and inserts an asynchronous FIFO. For the Cyclone V hard HPS this insertion fails at `qsys-generate` time with:
>
> ```
> EntityWritingException: Failed to write entity for hps_system_hps_0_hps_io_border
> ```
>
> **Fix:** Connect `clk_0.clk` to `hps_0.h2f_lw_axi_clock` (the same source as `led_pio.clk`). This eliminates the crossing entirely.

### 1.4 LED PIO on the LW bridge

The LED PIO is an 8-bit output register slave connected to the LW H2F AXI master of the HPS:

```tcl
add_instance led_pio altera_avalon_pio
set_instance_parameter_value led_pio width      {8}
set_instance_parameter_value led_pio direction  {Output}
set_instance_parameter_value led_pio resetValue {0}

# Reset the PIO from the HPS h2f_reset signal (active until HPS firmware releases it)
add_connection hps_0.h2f_reset led_pio.reset

# Connect the PIO as a slave on the bridge
add_connection hps_0.h2f_lw_axi_master led_pio.s1

# Base address within the 2 MB LW bridge window
# ARM address: 0xFF200000 + 0x0000 = 0xFF200000
set_connection_parameter_value hps_0.h2f_lw_axi_master/led_pio.s1 baseAddress {0x00000000}
```

### 1.5 Interface exports

Three interface groups are exported to become the top-level ports of `hps_system`:

```tcl
# FPGA fabric clock input (50 MHz from board oscillator)
add_interface clk clock sink
set_interface_property clk EXPORT_OF clk_0.clk_in

# FPGA fabric reset (active-low, from push-button KEY[0])
add_interface reset reset sink
set_interface_property reset EXPORT_OF clk_0.clk_in_reset

# DDR3 memory interface → HPS hard memory I/O pads
add_interface memory memory master
set_interface_property memory EXPORT_OF hps_0.memory

# LED outputs → 8 physical FPGA I/O pins
add_interface led_external_connection conduit end
set_interface_property led_external_connection EXPORT_OF led_pio.external_connection
```

Platform Designer generates a Verilog top-level entity called `hps_system` with ports named by the convention `<interface>_<signal>`. The exports above produce:

| Port name | Direction | Maps to |
|---|---|---|
| `clk_clk` | in | 50 MHz board clock |
| `reset_reset_n` | in | KEY[0] push-button |
| `memory_mem_a[14:0]` | out | DDR3 address bus |
| `memory_mem_dq[31:0]` | inout | DDR3 data bus |
| `memory_oct_rzqin` | in | OCT calibration reference |
| `led_external_connection_export[7:0]` | out | Board LEDs |
| *(and ~20 more DDR3 control/strobe pins)* | | |

The VHDL top-level wrapper (`hdl/de10_nano_top.vhd`) re-exports these as clean board-level port names: `fpga_clk1_50`, `key[0]`, `led[7:0]`, and `hps_ddr3_*`.

### 1.6 Generate the system

```bash
docker run --rm \
  -v /path/to/cvsoc:/work \
  raetro/quartus:23.1 \
  bash -c "cd /work/05_hps_led/quartus && make qsys"
```

`make qsys` runs two tools in sequence:

1. `qsys-script --script=hps_system.tcl` — reads the TCL and writes `hps_system.qsys`
2. `qsys-generate hps_system.qsys --synthesis=VERILOG` — generates ~77 HDL files into `hps_system/synthesis/`

A successful run ends with:

```
Info: Generating 21 modules from hps_system
Info: Generation complete
```

After generation succeeds, `make qsys` automatically runs `make patch-oct` (described next).

---

## Step 2 — Patch the DDR3 PHY

> **Why this step exists:** The Quartus 23.1 DDR3 soft model generated by Platform Designer has a known deficiency in the file `altdq_dqs2_acv_connect_to_hard_phy_cyclonev.sv`. If left unpatched, the Fitter raises two errors during place-and-route.

### 2.1 What goes wrong

The generated file declares a parameter at the top of the module:

```systemverilog
parameter USE_TERMINATION_CONTROL = "false"   // line ~97
```

The parent module (`hps_sdram_p0_altdqdqs.v`) overrides this to `"true"` via a `defparam`:

```systemverilog
defparam altdq_dqs2_inst.USE_TERMINATION_CONTROL = "true";
```

However, the module *never forwards* the parameter to the `cyclonev_io_obuf` primitive instances that have their `seriesterminationcontrol` port connected to a real control signal. Because of a Quartus elaboration limitation, the `defparam` override of a string-valued parameter is not propagated into primitive-level parameters.

The Fitter therefore sees primitives with `SERIESTERMINATIONCONTROL` connected but `use_termination_control="false"`, and raises:

```
Error (174068): [...] obuf_os_bar_0 [...] series OCT cannot be used
                because its USE_TERMINATION_CONTROL attribute is "false"
Error (174052): [...] memory_mem_dq[0] [...] I/O has dynamic termination
                control connected but does not use parallel termination
```

### 2.2 What the patch does

`scripts/patch_oct.py` hardcodes `.use_termination_control("true")` as a named port parameter on the five `cyclonev_io_obuf` instances that are affected:

| Instance | Location in file |
|---|---|
| `obuf_os_bar_0` | Differential DQS strobe-bar output |
| `obuf_os_0` | Bidirectional DQS strobe output |
| `data_out` (bidir generate loop) | Per-bit DQ bidirectional output |
| `data_out` (output generate loop) | Per-bit DQ unidirectional output |
| `obuf_1` (extra_output_pad_gen loop) | Data mask (DM) output |

Each patch is idempotent: the script checks whether the new text already exists before modifying the file.

### 2.3 Running the patch

The patch is run automatically by `make qsys`. To run it manually:

```bash
docker run --rm \
  -v /path/to/cvsoc:/work \
  raetro/quartus:23.1 \
  python3 /work/05_hps_led/scripts/patch_oct.py \
    /work/05_hps_led/qsys/hps_system/synthesis/submodules/\
altdq_dqs2_acv_connect_to_hard_phy_cyclonev.sv
```

Expected output:

```
  patched obuf_os_bar_0
  patched obuf_os_0
  patched data_out (bidir)
  patched data_out (output)
  patched obuf_1
  wrote .../altdq_dqs2_acv_connect_to_hard_phy_cyclonev.sv
```

---

## Step 3 — Create the Quartus project

### 3.1 The QIP-only strategy

In Phase 2 the Quartus project included a `QSYS_FILE` assignment. That approach is **not used here**. The reason matters:

When Quartus encounters `QSYS_FILE` in a project, it extracts all IP source files from its internal installation into `db/ip/<system_name>/`. These extracted files are the *original, unpatched* versions from the Quartus installation. The patched `altdq_dqs2_acv_connect_to_hard_phy_cyclonev.sv` in your `qsys/` directory is silently overridden.

The fix is to use only `QIP_FILE`:

```tcl
# de10_nano_project.tcl

project_new 05_hps_led -revision de10_nano -overwrite

set_global_assignment -name FAMILY "Cyclone V"
set_global_assignment -name DEVICE 5CSEBA6U23I7
set_global_assignment -name SDC_FILE de10_nano.sdc

# VHDL top-level wrapper — connects board pins to the Platform Designer system
set_global_assignment -name TOP_LEVEL_ENTITY de10_nano_top
set_global_assignment -name VHDL_FILE        ../hdl/de10_nano_top.vhd

# QIP only — NOT QSYS_FILE. Using QIP prevents Quartus from extracting
# unpatched DDR3 IP files from its installation into db/ip/.
set_global_assignment -name QIP_FILE         ../qsys/hps_system/synthesis/hps_system.qip

source de10_nano_pin_assignments.tcl
project_close
```

> **Key learning #2 — Never use `QSYS_FILE` when you have patched generated files.**  
> `QSYS_FILE` tells Quartus to manage IP extraction itself, overwriting any local modifications to generated files. If your build relies on patched generated IP, reference it exclusively through `QIP_FILE`. The `QIP_FILE` path causes Quartus to use your files directly without extraction.

### 3.2 Port names and pin assignments

The VHDL top-level wrapper (`hdl/de10_nano_top.vhd`) exposes clean board-level port names. Pin assignments reference these wrapper ports:

| Physical signal | VHDL top-level port | Pin |
|---|---|---|
| 50 MHz oscillator | `fpga_clk1_50` | V11 |
| KEY[0] push-button | `key[0]` | AH17 |
| LED0 | `led[0]` | W15 |
| LED1 | `led[1]` | AA24 |
| LED2 | `led[2]` | V16 |
| LED3 | `led[3]` | V15 |
| LED4 | `led[4]` | AF26 |
| LED5 | `led[5]` | AE26 |
| LED6 | `led[6]` | Y16 |
| LED7 | `led[7]` | AA23 |

The DDR3 pins (`memory_mem_*`) are constrained automatically by Quartus from the HPS component parameters — no explicit `set_location_assignment` is needed or allowed for hard-IP pins.

### 3.3 Timing constraints

`de10_nano.sdc` references the VHDL wrapper clock port name:

```tcl
# Port name is fpga_clk1_50 — the VHDL top-level wrapper port.
create_clock -name {FPGA_CLK1_50} -period 20.000 [get_ports {fpga_clk1_50}]

derive_pll_clocks
derive_clock_uncertainty
```

Create the project:

```bash
docker run --rm \
  -v /path/to/cvsoc:/work \
  raetro/quartus:23.1 \
  bash -c "cd /work/05_hps_led/quartus && make project"
```

---

## Step 4 — Compile with the two-pass flow

### 4.1 Why a single-pass compile fails

The `hps_sdram_p0_pin_assignments.tcl` file (generated alongside the QIP by `qsys-generate`) assigns the DDR3 I/O standards (`SSTL-135`) and OCT calibration settings (`INPUT_TERMINATION`, `OUTPUT_TERMINATION`) to all DDR3 pins. Without these assignments the Fitter raises Error 174052 on every DDR3 DQ pin.

This script cannot be evaluated during Quartus project creation (`quartus_sh -t`) because it needs an existing **post-synthesis timing netlist** to run. If it runs before any synthesis has occurred, it bootstraps itself by spawning `quartus_sta` — which then fails because no timing netlist exists yet.

A standard `quartus_sh --flow compile` also fails for the same reason: it tries to apply the pin assignments before synthesis is complete.

### 4.2 The two-pass sequence

The solution is a four-stage compile with an explicit pin assignment step between the two synthesis passes:

```
Pass 1: quartus_map    → synthesis; creates timing netlist
Apply:  quartus_sta -t hps_sdram_p0_pin_assignments.tcl de10_nano
                       → applies DDR3 I/O standards and OCT settings to QSF
Pass 2: quartus_map    → re-synthesises, picks up new QSF assignments
        quartus_fit    → place & route (DDR3 OCT errors now resolved)
        quartus_asm    → generate .sof bitstream
        quartus_sta    → final timing analysis
```

The Makefile `compile` target implements this sequence:

```makefile
compile:
	quartus_map  --read_settings_files=on --write_settings_files=off \
	             $(PROJECT_NAME) -c $(REVISION_NAME)
	quartus_sta  -t $(QSYS_GEN_DIR)/submodules/hps_sdram_p0_pin_assignments.tcl \
	             $(REVISION_NAME)
	quartus_map  --read_settings_files=on --write_settings_files=off \
	             $(PROJECT_NAME) -c $(REVISION_NAME)
	quartus_fit  --read_settings_files=on --write_settings_files=off \
	             $(PROJECT_NAME) -c $(REVISION_NAME)
	quartus_asm  --read_settings_files=on --write_settings_files=off \
	             $(PROJECT_NAME) -c $(REVISION_NAME)
	quartus_sta  $(PROJECT_NAME) -c $(REVISION_NAME)
```

> **Key learning #3 — Timing Analyzer (`quartus_sta`) writes back to the QSF.**  
> When `quartus_sta` runs `hps_sdram_p0_pin_assignments.tcl` in timing analysis context (after `quartus_map` has produced a timing netlist), `timing_netlist_exist` evaluates to true inside the script. The script then directly applies its `set_instance_assignment` and `set_location_assignment` calls to the open project — modifying the `.qsf` file. This is why the second `quartus_map` pass picks up the DDR3 pin assignments even though they were not in the `.qsf` when the project was first created.

### 4.3 Running the full compile

```bash
docker run --rm \
  -v /path/to/cvsoc:/work \
  raetro/quartus:23.1 \
  bash -c "cd /work/05_hps_led/quartus && make compile"
```

Expected output at each stage:

```
Info: Quartus Prime Analysis & Synthesis was successful. 0 errors, N warnings
Info: Quartus Prime Timing Analyzer was successful. 0 errors, 1 warning
Info: Quartus Prime Analysis & Synthesis was successful. 0 errors, N warnings
Info: Quartus Prime Fitter was successful. 0 errors, 196 warnings
Info: Quartus Prime Assembler was successful. 0 errors, 2 warnings
Info: Quartus Prime Timing Analyzer was successful. 0 errors, 191 warnings
```

The bitstream is written to `de10_nano.sof` in the `quartus/` directory.

---

## Step 5 — Write the ARM bare-metal application

Three files make up the software: `startup.S`, `linker.ld`, and `main.c`.

### 5.1 The startup assembly

`software/app/startup.S` is the reset entry point. It runs before `main()`:

```asm
/* ARM Exception Vector Table — must be at offset 0 of the binary.
 * When L3_REMAP maps OCRAM at 0x00000000, the CPU uses this table for
 * all exceptions.  Each entry branches to _start so the system
 * re-initialises cleanly on any unexpected trap. */
_vectors:
    b   _start          /* 0x00: Reset                */
    b   _start          /* 0x04: Undefined Instruction */
    b   _start          /* 0x08: Software Interrupt    */
    b   _start          /* 0x0C: Prefetch Abort        */
    b   _start          /* 0x10: Data Abort            */
    b   _start          /* 0x14: Reserved              */
    b   _start          /* 0x18: IRQ                   */
    b   _start          /* 0x1C: FIQ                   */

_start:
    msr  cpsr_c, #0xD3        /* SVC mode, IRQ and FIQ disabled */
    ldr  sp, =_stack_top      /* stack top = end of OCRAM        */
    /* zero .bss */
    ldr  r0, =__bss_start
    ldr  r1, =__bss_end
    mov  r2, #0
.Lbss_loop:
    cmp  r0, r1
    strlt r2, [r0], #4
    blt  .Lbss_loop
    bl   main                 /* call application                */
.Lhalt:
    b    .Lhalt               /* main() must never return        */
```

Setting the CPU to SVC mode with interrupts disabled is the standard bare-metal starting state for Cortex-A9. In this mode, all system control registers are writable and no exception can interrupt bridge initialisation.

> **Why the vector table is required:** `hps_bridge_init()` writes to the L3 REMAP register which maps OCRAM at address `0x00000000`. From that point the CPU's exception vector base points into our binary. Without an explicit vector table, any exception (undefined instruction, data abort, etc.) would jump to whatever code happens to live at offset `0x04` in the binary — which may itself be an ARM instruction from the middle of another function, leaving the CPU in an exception mode with a corrupted stack. The vector table ensures every unexpected exception re-enters `_start` and re-initialises in SVC mode.

### 5.2 The linker script

`software/app/linker.ld` places the entire application in OCRAM:

```ld
MEMORY {
    OCRAM (rwx) : ORIGIN = 0xFFFF0000, LENGTH = 64K
}

SECTIONS {
    .startup : { *(.startup) } > OCRAM   /* must be first: _start = 0xFFFF0000 */
    .text    : { *(.text*) *(.rodata*) } > OCRAM
    .data    : { *(.data*) }             > OCRAM
    .bss     : {
        __bss_start = .;
        *(.bss*) *(COMMON)
        __bss_end   = .;
    } > OCRAM
    _stack_top = ORIGIN(OCRAM) + LENGTH(OCRAM);  /* 0xFFFFFFFF + 1 */
}
```

Placing `.startup` first ensures that `_start` is at `0xFFFF0000`. When the JTAG debugger loads the ELF file and sets the PC to the entry point, execution begins at `_start`.

### 5.3 Bridge and system initialisation in main.c

Before writing to any FPGA peripheral, the HPS firmware must complete **three** initialisation steps. Missing any one of them produces a silent data abort or an unexpected reset.

#### Step A — L3 REMAP register

The L3 (NIC-301) interconnect REMAP register at `0xFF800000` controls which address regions are visible to the CPU. After a cold reset (or JTAG programming), **bit 4** (`LWHPS2FPGA`) is cleared — the Lightweight HPS-to-FPGA bridge address window at `0xFF200000` is completely invisible. Any CPU access to this range returns a DECERR at the L3 level, which the ARM core sees as a Synchronous External Abort (DFSR = `0x08`).

```c
/* L3 (NIC-301) interconnect REMAP register.
 * Bit 4: make LW H2F bridge address window visible at 0xFF200000.
 * Bit 0: map OCRAM at 0x00000000 (needed for exception vector table). */
#define L3_REMAP              (*(volatile uint32_t *)0xFF800000UL)
#define L3_REMAP_OCRAM        (1u << 0)
#define L3_REMAP_LWHPS2FPGA   (1u << 4)
```

> **Note:** This register appears write-only — reads return `0x00000000` even after a successful write — but the functional effect is immediate. This is the same write that U-Boot's `socfpga_bridges_enable()` performs in `arch/arm/mach-socfpga/misc_gen5.c`. In a bare-metal JTAG-load scenario, the application must do it manually.

#### Step B — BRGMODRST bridge reset toggle

Even with L3 REMAP set, the FPGA LED PIO peripheral may be held in reset by the `hps_0.h2f_rst_n` signal. In the generated `hps_system` fabric, `h2f_rst_n` feeds an `altera_reset_controller` that drives `led_pio.reset_n`. While LOW, the Avalon bus interface of the LED PIO does not respond.

`h2f_rst_n` is asserted (LOW) by the FPGA Manager during JTAG configuration. To pulse it HIGH, the firmware must explicitly assert **and then release** all three bridge resets:

```c
#define RSTMGR_BASE           0xFFD05000UL
#define RSTMGR_BRGMODRST      (*(volatile uint32_t *)(RSTMGR_BASE + 0x01CUL))
#define BRGMODRST_HPS2FPGA    (1u << 0)
#define BRGMODRST_LWHPS2FPGA  (1u << 1)   /* note: bit 1, not bit 2 */
#define BRGMODRST_FPGA2HPS    (1u << 2)
#define BRGMODRST_ALL         (BRGMODRST_HPS2FPGA | BRGMODRST_LWHPS2FPGA | BRGMODRST_FPGA2HPS)
```

The complete `hps_bridge_init()` function:

```c
static void hps_bridge_init(void)
{
    /* Step A: make LW H2F bridge and OCRAM-at-0x0 visible in L3 */
    L3_REMAP = L3_REMAP_LWHPS2FPGA | L3_REMAP_OCRAM;

    /* Step B: toggle all bridge resets to pulse h2f_rst_n into FPGA fabric */
    RSTMGR_BRGMODRST |=  BRGMODRST_ALL;
    delay(200000UL);
    RSTMGR_BRGMODRST &= ~BRGMODRST_ALL;
    delay(200000UL);
}
```

#### Step C — Watchdog timers

The Cyclone V HPS has **three** hardware watchdog timers that can reset the CPU unexpectedly:

| Timer | Address | Disableable? | Strategy |
|---|---|---|---|
| L4 WDT0 | `0xFFD02000` | No (`WDT_ALWAYS_EN=1`) | Set max TORR + periodic kick |
| L4 WDT1 | `0xFFD03000` | No (`WDT_ALWAYS_EN=1`) | Set max TORR + periodic kick |
| MPCore Private WDT | `0xFFFEC620` | Yes | Disable via magic unlock sequence |

> **Critical:** The L4 watchdogs have `WDT_ALWAYS_EN = 1` — the enable bit in `WDT_CR` is **read-only** and permanently set. Reading `WDT_CR` may return `0`, but the watchdog IS running. The only option is to set the maximum timeout and periodically "kick" it.

```c
#define WDT0_BASE            0xFFD02000UL
#define WDT1_BASE            0xFFD03000UL
#define WDT_TORR(base)       (*(volatile uint32_t *)((base) + 0x04UL))
#define WDT_CRR(base)        (*(volatile uint32_t *)((base) + 0x0CUL))
#define WDT_KICK_VALUE       0x76u

#define MPCORE_WDT_BASE      0xFFFEC620UL
#define MPCORE_WDT_DISABLE   (*(volatile uint32_t *)(MPCORE_WDT_BASE + 0x14UL))
#define MPCORE_WDT_CTRL      (*(volatile uint32_t *)(MPCORE_WDT_BASE + 0x08UL))

static void wdt_init(void)
{
    WDT_TORR(WDT0_BASE) = 0xFFu;    /* max timeout: TOP=0xF, TOP_INIT=0xF */
    WDT_CRR(WDT0_BASE)  = WDT_KICK_VALUE;
    WDT_TORR(WDT1_BASE) = 0xFFu;
    WDT_CRR(WDT1_BASE)  = WDT_KICK_VALUE;

    /* MPCore private watchdog: unlock then stop */
    MPCORE_WDT_DISABLE = 0x12345678u;
    MPCORE_WDT_DISABLE = 0x87654321u;
    MPCORE_WDT_CTRL    = 0;
}

static inline void wdt_kick(void)
{
    WDT_CRR(WDT0_BASE) = WDT_KICK_VALUE;
    WDT_CRR(WDT1_BASE) = WDT_KICK_VALUE;
}
```

All register addresses are from the Cyclone V Hard Processor System Technical Reference Manual (TRM). You do not need the HAL or any generated header file — the TRM provides all addresses directly.

### 5.4 The LED pattern loop

```c
/* LED PIO DATA register at LW bridge base + offset 0x0000 */
#define LED_PIO_DATA  (*(volatile uint32_t *)(0xFF200000UL))

static const uint8_t patterns[] = {
    0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x7F, 0xFF,  /* fill up   */
    0x7F, 0x3F, 0x1F, 0x0F, 0x07, 0x03, 0x01, 0x00,  /* drain     */
    0xAA, 0x55, 0xAA, 0x55,                            /* stripes   */
    0xFF, 0x00,                                        /* on / off  */
};

void main(void)
{
    uint32_t idx = 0;
    const uint32_t num_patterns = sizeof(patterns) / sizeof(patterns[0]);

    wdt_init();         /* handle all three watchdogs first */
    hps_bridge_init();  /* L3 REMAP + bridge reset toggle   */

    while (1) {
        wdt_kick();     /* must be called before watchdog timeout */
        LED_PIO_DATA = patterns[idx];
        idx++;
        if (idx >= num_patterns)
            idx = 0;
        delay(2000000UL);
    }
}
```

> **Key learning #4 — Always call `wdt_init()` before `hps_bridge_init()`.**  
> The watchdog timers are already running when your JTAG-loaded code begins. On a Cyclone V, the L4 WDTs expire in ~2–30 seconds depending on the TORR value set by boot code. Call `wdt_init()` as the very first thing in `main()`, before any other initialisation — including the bridge and delay calls in `hps_bridge_init()`.

> **Key learning #5 — Do not use the modulo operator (`%`) with `-nostdlib`.**  
> The ARM GCC toolchain implements integer division (including modulo) by calling the runtime helper `__aeabi_uidivmod`. With `-nostdlib` the helper is not linked in, so any use of `%` causes a link error. The pattern index wraparound uses an explicit comparison and reset (`if (idx >= num_patterns) idx = 0`) instead.

### 5.5 Build the application

Install the ARM cross-compiler (one-time setup inside the Docker container):

```bash
docker run --rm -v /path/to/cvsoc:/work raetro/quartus:23.1 \
  bash -c "cd /work/05_hps_led/quartus && make setup"
```

Then build:

```bash
docker run --rm -v /path/to/cvsoc:/work raetro/quartus:23.1 \
  bash -c "cd /work/05_hps_led/quartus && make app"
```

Expected output:

```
arm-linux-gnueabihf-gcc -mcpu=cortex-a9 -mfpu=neon-vfpv4 ... main.c startup.S -o hps_led.elf
arm-linux-gnueabihf-objcopy -O binary hps_led.elf hps_led.bin
   text    data     bss     dec
    684       0       4     688   hps_led.elf
```

The `.elf` is 688 bytes — trivially small for 64 KB of OCRAM.

---

## Step 6 — Build everything and program the board

### 6.1 Full build in one command

The `setup` step only needs to run once per fresh container session. After that, the full hardware + software build is:

```bash
docker run --rm \
  -v /path/to/cvsoc:/work \
  raetro/quartus:23.1 \
  bash -c "
    cd /work/05_hps_led/quartus && \
    make setup && \
    make qsys project compile app
  "
```

`make all` runs the same sequence: `qsys → project → compile → app`. To start completely fresh:

```bash
docker run --rm \
  -v /path/to/cvsoc:/work \
  raetro/quartus:23.1 \
  bash -c "
    cd /work/05_hps_led/quartus && \
    make setup && \
    make clean && make all
  "
```

### 6.2 Program the FPGA

From the `05_hps_led/quartus/` directory, use the Makefile target. On a WSL2/Docker setup the Makefile handles USB ownership automatically (detaching from WSL2, programming via Windows `quartus_pgm.exe`, then re-attaching to WSL2):

```bash
cd 05_hps_led/quartus
make program-sof
```

The bitstream is loaded into the FPGA SRAM configuration (volatile; lost on power cycle).

### 6.3 Load and run the firmware

Transfer the raw binary to OCRAM and start execution via OpenOCD running inside Docker:

```bash
cd 05_hps_led/quartus
make download-elf
```

Expected output:

```
Attaching USB-Blaster (busid 2-4) to WSL2...
524 bytes written at address 0xffff0000
downloaded 524 bytes in 0.17s (3.0 KiB/s)
Done: HPS LED application is running.
```

> **How it works:** `download-elf` starts a `jtagd` daemon inside the `raetro/quartus:23.1` Docker container, then launches OpenOCD with the `aji_client` interface (the only JTAG interface supported by that build). OpenOCD halts the CPU, loads the raw `.bin` file into OCRAM at `0xFFFF0000`, sets PC to the entry point, and resumes execution. See `05_hps_led/scripts/de10_nano_hps.cfg` for the full OpenOCD configuration.

### 6.4 Observe the LEDs

The eight LEDs on the DE10-Nano cycle through the sequence:

1. **Fill up** — `▪░░░░░░░` → `▪▪▪▪▪▪▪▪` (8 steps)
2. **Drain** — `▪▪▪▪▪▪▪░` → `░░░░░░░░` (8 steps)
3. **Alternating stripes** — `▪░▪░▪░▪░` ↔ `░▪░▪░▪░▪` (4 steps)
4. **All on / all off** — `▪▪▪▪▪▪▪▪` → `░░░░░░░░` (2 steps)

Each step lasts approximately 320 ms (2 000 000 busy-loop iterations at ~25 MHz osc1_clk in JTAG-load mode, where PLLs are not configured).

---

## Key learnings

These are the non-obvious problems you will encounter when building this project from scratch. Each one is a real debugging trap.

### 1. Drive `h2f_lw_axi_clock` from the same clock as `led_pio`

If `clk_0` drives `led_pio.clk` and a *separate* exported clock pin drives `hps_0.h2f_lw_axi_clock`, Platform Designer inserts a clock-crossing FIFO between the bridge master and the PIO slave. For Cyclone V, this insertion fails at `qsys-generate` time:

```
Error: EntityWritingException: Failed to write entity for hps_system_hps_0_hps_io_border
Info: Generating 0 files from hps_system
```

**Fix:** `add_connection clk_0.clk hps_0.h2f_lw_axi_clock` — same source as `led_pio.clk`.

### 2. Never use `QSYS_FILE` with patched generated IP

Any `QSYS_FILE` assignment in your Quartus project (or `.qsf`) causes Quartus to extract IP files from its internal installation into `db/ip/`, overwriting any patched files in your `synthesis/` directory. The patch is silently lost and the 174068/174052 errors reappear.

**Fix:** Use `QIP_FILE` exclusively. Never add `QSYS_FILE` to the project.

### 3. The two-pass compile is mandatory for Cyclone V HPS DDR3

`hps_sdram_p0_pin_assignments.tcl` must run in `quartus_sta` context (after a timing netlist exists) to apply DDR3 I/O standard and OCT calibration assignments. A single `quartus_sh --flow compile` does not satisfy this requirement.

**Fix:** Use the explicit four-stage sequence: `quartus_map` → `quartus_sta -t hps_sdram_p0_pin_assignments.tcl` → `quartus_map` → `quartus_fit` → `quartus_asm` → `quartus_sta`.

### 4. The LW bridge requires L3 REMAP _and_ BRGMODRST — in that order

After JTAG programming, the Lightweight HPS-to-FPGA bridge is invisible to the CPU **and** held in reset. Two separate actions are required; missing either one causes a silent data abort on the first write to `0xFF200000`:

1. **L3 REMAP** (`0xFF800000` bit 4) — makes the bridge address window visible in the L3 interconnect. Without this, the L3 returns DECERR → Synchronous External Abort (DFSR=`0x08`). This register appears write-only (reads return `0`).
2. **BRGMODRST toggle** — assert all three bridge reset bits then release them to pulse `h2f_rst_n` into the FPGA fabric, clearing the reset on `led_pio.reset_n`.

**Fix:** Call `L3_REMAP = (1u << 4) | (1u << 0)`, then toggle `RSTMGR_BRGMODRST` with all three bits. See `hps_bridge_init()` in `main.c`.

**Common mistakes that still leave the LEDs off:**

| Mistake | Result |
|---|---|
| Missing L3_REMAP write | DECERR data abort on first bridge access |
| `BRGMODRST_LWHPS2FPGA = (1u << 2)` | Wrong bit — bit 2 is FPGA2HPS; LWHPS2FPGA is bit 1 |
| Clearing only the LWHPS2FPGA bit | `h2f_rst_n` may not pulse cleanly; release all three |
| Writing `SYSMGR_FPGAINTF_EN_2` | This register controls trace/JTAG mux, not AXI bridges |

### 5. All three watchdog timers must be handled

The Cyclone V has two L4 APB watchdogs (`WDT_ALWAYS_EN=1` — cannot be disabled) and one MPCore private watchdog. Missing any one causes an unexpected warm reset:

- WDT0 / WDT1: set `TORR = 0xFF` (max period) and write `0x76` to `CRR` before the counter expires
- MPCore WDT: disable via the `0x12345678` / `0x87654321` magic unlock sequence, then clear `CTRL`
- Call `wdt_kick()` in the main loop — a 2 000 000-iteration delay takes ~320 ms at 25 MHz (longer than many default timeout periods)

**Fix:** Call `wdt_init()` as the **first** thing in `main()`, before any delay or bridge access. Kick both L4 watchdogs at the top of every main loop iteration.

### 6. Do not use `%` with `-nostdlib`

Integer division with `%` or `/` causes the compiler to emit a call to `__aeabi_uidivmod` or `__aeabi_idiv`. With `-nostdlib` these helpers are not available and the linker fails:

```
undefined reference to `__aeabi_uidivmod'
```

**Fix:** Replace modulo with an explicit compare-and-reset pattern, or link against `libgcc` (add `-lgcc` to `LDFLAGS`).

### 7. ARM GCC flag: use `-mfpu=neon-vfpv4`, not `neon-vfpv3`

The `raetro/quartus:23.1` container includes GCC 6.x for the ARM cross-toolchain. GCC 6 does not recognise `-mfpu=neon-vfpv3`. The Cortex-A9 in the Cyclone V SoC supports VFPv4, so `-mfpu=neon-vfpv4` is both correct and accepted by GCC 6.

### 8. The exception vector table must be at offset 0 when OCRAM is remapped

`hps_bridge_init()` writes `L3_REMAP` with bit 0 set, which maps OCRAM at address `0x00000000`. From that point, ARM exception vectors (`0x00000000`–`0x0000001C`) point into our binary. If the binary has no vector table, any exception will branch to whatever instruction happens to be at offset `0x04` in the binary, leaving the CPU stuck in an exception mode (UND, Abort, etc.) with a corrupted stack.

**Fix:** Place an 8-entry ARM branch table at the very start of `.startup` (before `_start`). Each entry should `b _start` so that any unexpected exception re-enters the initialisation sequence cleanly in SVC mode.

---

## Summary

You have built a complete HPS bare-metal system using a fully scripted workflow:

| What | How |
|---|---|
| Defined hardware | TCL script → `qsys-script` + `qsys-generate` |
| Fixed DDR3 PHY deficiency | `scripts/patch_oct.py` |
| Created Quartus project | `quartus_sh -t de10_nano_project.tcl` (QIP-only) |
| Compiled FPGA bitstream | Two-pass: `quartus_map` → pin assignments → `quartus_map` → `quartus_fit` → `quartus_asm` |
| Cross-compiled ARM firmware | `arm-linux-gnueabihf-gcc` with `startup.S` + `linker.ld` |
| Deployed to board | `make program-sof` + `make download-elf` (WSL2/Docker JTAG workflow) |

No GUI was opened at any point. Every step is reproducible by `make all`.

---

## What's next — Phase 4: Hardware Interrupts

Phase 3 polled the LED peripheral in a tight loop. Phase 4 replaces polling with **hardware interrupts** driven by the DE10-Nano push buttons (`KEY[0]` / `KEY[1]`). You will:

- Extend both the Nios II and the HPS Platform Designer systems with a **button PIO** component configured in edge-triggered interrupt mode
- Handle interrupts on the **Nios II** using the HAL `alt_ic_isr_register()` API (project `06_nios2_interrupts`)
- Handle interrupts on the **ARM Cortex-A9** by configuring the **Generic Interrupt Controller (GIC)**, setting up the ARM exception vector table, and writing an IRQ service routine (project `07_hps_interrupts`)
- Learn interrupt latency, volatile variables, race conditions, and hardware debouncing

The LED peripheral built in this phase is reused unchanged — only the trigger mechanism changes.

Continue to `docs/tutorial_phase4_interrupts.md` *(coming soon)*.

> **Looking further ahead:** Phase 5 boots **Embedded Linux** (Buildroot) on the same ARM core and controls the LED via a device tree overlay and the FPGA Manager sysfs interface. Phase 6 adds **Ethernet** control from a PC over UDP.
