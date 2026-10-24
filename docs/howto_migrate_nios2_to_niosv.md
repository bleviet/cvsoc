# How-to: Migrate a Soft-Core Project from Nios II (Quartus 23.1) to Nios V/m RISC-V (Quartus 25.1)

> **Series:** cvsoc — Stepping into advanced FPGA development on the DE10-Nano  
> **Type:** How-to Guide (Problem-oriented)  
> **Difficulty:** Intermediate  
> **Applies to:** Phase 2 (`04_nios2_led` → `04b_niosv_led`), Phase 4 (`06_nios2_interrupts` → `06b_niosv_interrupts`), Phase 5 (`08_nios2_debug` → `08b_niosv_debug`), Phase 11 ([`tutorial_phase11_multi_version_quartus.md`](tutorial_phase11_multi_version_quartus.md))

---

## Overview

Intel has deprecated the classic Nios II processor (`altera_nios2_gen2`) in Quartus Prime 25.1 in favor of the **Nios V/m (`intel_niosv_m`)**—a 32-bit RISC-V (`RV32I` + `Zicsr`) soft-core processor.

This guide gives you a concrete, step-by-step recipe to migrate any Nios II Platform Designer system, Makefile, BSP, and C/C++ firmware to Nios V/m on Quartus 25.1.

---

## Step 1 — Update the Project Makefile Manifest Variables

In your project's `quartus/Makefile`, pin the supported Quartus version to `25.1` before including `common/make/quartus-version.mk`:

```makefile
# Before (Nios II — Quartus 23.1):
PROJECT_NAME      = 04_nios2_led
REVISION_NAME     = de10_nano
QUARTUS_SUPPORTED ?= 23.1
QUARTUS_DEFAULT   ?= 23.1

# After (Nios V/m — Quartus 25.1):
PROJECT_NAME      = 04b_niosv_led
REVISION_NAME     = de10_nano
QUARTUS_SUPPORTED ?= 25.1
QUARTUS_DEFAULT   ?= 25.1

include ../../common/make/quartus-version.mk
```

---

## Step 2 — Migrate the Platform Designer Tcl Script (`qsys/*.tcl`)

Open your `qsys/nios2_system.tcl` and apply the following 6 replacements to convert it into `qsys/niosv_system.tcl` (compare [`04_nios2_led/qsys/nios2_system.tcl`](../04_nios2_led/qsys/nios2_system.tcl) with [`04b_niosv_led/qsys/niosv_system.tcl`](../04b_niosv_led/qsys/niosv_system.tcl)):

### 2.1 Update Qsys Package Version and Explicit Clock Rate

`niosv-bsp` requires the clock bridge to declare an explicit clock rate so it can populate `ALT_CPU_FREQ` in `system.h`:

```tcl
# Before:
package require -exact qsys 12.0
add_instance clk_0 altera_clock_bridge
set_instance_parameter_value clk_0 NUM_CLOCK_OUTPUTS 1

# After:
package require -exact qsys 16.1
add_instance clk_0 altera_clock_bridge
set_instance_parameter_value clk_0 NUM_CLOCK_OUTPUTS 1
set_instance_parameter_value clk_0 EXPLICIT_CLOCK_RATE {50000000}
```

### 2.2 Replace `altera_nios2_gen2` with `intel_niosv_m`

Enable the Avalon-MM interface (`enableAvalonInterface {True}`) on `intel_niosv_m` so it connects directly to Avalon-MM slaves without AXI adapters:

```tcl
# Before (Nios II/e):
add_instance nios2 altera_nios2_gen2
set_instance_parameter_value nios2 impl {Tiny}
set_instance_parameter_value nios2 setting_preciseIllegalMemAccessException {0}

# After (Nios V/m RISC-V):
add_instance niosv intel_niosv_m
set_instance_parameter_value niosv enableAvalonInterface {True}
```

### 2.3 Rename Bus Masters and Connect `dm_agent` + `timer_sw_agent`

Nios V renames `data_master` → `data_manager`, `instruction_master` → `instruction_manager`, and replaces `debug_mem_slave` with **two** internal slave agents:
- `niosv.dm_agent` (RISC-V External Debug Module, span `64 KB`, place at `0x00100000`)
- `niosv.timer_sw_agent` (RISC-V CLINT machine timer & software interrupt block, place at `0x00010140`)

```tcl
# Avalon-MM Data Bus:
add_connection niosv.data_manager onchip_mem.s1
add_connection niosv.data_manager jtag_uart.avalon_jtag_slave
add_connection niosv.data_manager sysid.control_slave
add_connection niosv.data_manager led_pio.s1
add_connection niosv.data_manager niosv.timer_sw_agent
add_connection niosv.data_manager niosv.dm_agent

# Avalon-MM Instruction Bus:
add_connection niosv.instruction_manager onchip_mem.s1
add_connection niosv.instruction_manager niosv.dm_agent

# Base Addresses:
set_connection_parameter_value niosv.data_manager/onchip_mem.s1               baseAddress {0x00000000}
set_connection_parameter_value niosv.instruction_manager/onchip_mem.s1        baseAddress {0x00000000}
set_connection_parameter_value niosv.data_manager/led_pio.s1                  baseAddress {0x00010010}
set_connection_parameter_value niosv.data_manager/jtag_uart.avalon_jtag_slave baseAddress {0x00010100}
set_connection_parameter_value niosv.data_manager/sysid.control_slave         baseAddress {0x00010108}
set_connection_parameter_value niosv.data_manager/niosv.timer_sw_agent        baseAddress {0x00010140}
set_connection_parameter_value niosv.data_manager/niosv.dm_agent              baseAddress {0x00100000}
set_connection_parameter_value niosv.instruction_manager/niosv.dm_agent       baseAddress {0x00100000}
```

### 2.4 Rename the Interrupt Receiver Port (`nios2.irq` → `niosv.platform_irq_rx`)

```tcl
# Before (Nios II):
add_connection nios2.irq jtag_uart.irq
set_connection_parameter_value nios2.irq/jtag_uart.irq irqNumber {0}

# After (Nios V/m):
add_connection niosv.platform_irq_rx jtag_uart.irq
set_connection_parameter_value niosv.platform_irq_rx/jtag_uart.irq irqNumber {0}
```

### 2.5 Update Reset Vector Configuration

Unlike Nios II (which sets both `resetSlave`/`resetOffset` and `exceptionSlave`/`exceptionOffset`), Nios V sets `resetSlave {Absolute}` and `resetOffset {0x00000000}` (the RISC-V trap vector `mtvec` is initialized by the HAL startup code):

```tcl
set_instance_parameter_value niosv resetSlave  {Absolute}
set_instance_parameter_value niosv resetOffset {0x00000000}
```

---

## Step 3 — Migrate the BSP and Application Build Flow (Makefile → CMake)

The Nios V toolchain replaces `nios2-bsp` + `nios2-app-generate-makefile` with **`niosv-bsp`** + **`niosv-app`** (which generates a `CMakeLists.txt` built by CMake and `riscv32-unknown-elf-gcc`).

### 1. Generate the Nios V BSP (`quartus/Makefile`)

```makefile
bsp:
	$(call QTOOL, \
	  niosv-bsp -c --quartus-project=$(PROJECT_NAME).qpf \
	    --sopcinfo=niosv_system.sopcinfo \
	    --type=hal \
	    ../software/bsp/settings.bsp)
```

### 2. Generate and Build the Nios V Application (`software/app/Makefile`)

See [`06b_niosv_interrupts/software/app/Makefile`](../06b_niosv_interrupts/software/app/Makefile) for the complete pattern:

```makefile
APP_NAME := niosv_interrupts
BSP_DIR  := ../bsp
SRCS     := main.c

all: build/$(APP_NAME).elf

CMakeLists.txt: $(SRCS)
	niosv-app --app-dir=. --bsp-dir=$(BSP_DIR) --srcs=$(SRCS) --elf-name=$(APP_NAME).elf

build/$(APP_NAME).elf: CMakeLists.txt $(SRCS)
	cmake -S . -B build -G "Unix Makefiles"
	$(MAKE) -C build
```

---

## Step 4 — Update Firmware Download, Terminal, and GDB Commands

| Task | Nios II (Quartus 23.1) | Nios V/m (Quartus 25.1) |
|---|---|---|
| **Download & run ELF over JTAG** | `nios2-download -g -r app.elf` | `niosv-download -g -r app.elf` |
| **Open JTAG UART console** | `nios2-terminal` | `juart-terminal` |
| **Start JTAG GDB server** | `nios2-gdb-server --tcpport 2345 --tcppersist` | `ash-riscv-gdb-server --device 02D020DD --gdb-port 3333` |
| **Start GDB client** | `nios2-elf-gdb app.elf` | `riscv32-unknown-elf-gdb app.elf` |

> **Good news for C Firmware (`main.c`):** Because Intel preserved the HAL API (`system.h`, `altera_avalon_pio_regs.h`, `IORD_ALTERA_AVALON_PIO_EDGE_CAP`, `IOWR_ALTERA_AVALON_PIO_DATA`, and `alt_ic_isr_register()`) across both Nios II and Nios V, [`06_nios2_interrupts/software/app/main.c`](../06_nios2_interrupts/software/app/main.c) and [`06b_niosv_interrupts/software/app/main.c`](../06b_niosv_interrupts/software/app/main.c) are **100% source-code compatible**!

---

## Related Documentation

- **Explanations:**
  - [Explanation: Soft-Core Processors in FPGA Fabric — Nios II vs. Nios V/m](explanation_phase2_soft_cpu_nios2_and_niosv.md)
  - [Explanation: Hardware Interrupt Subsystems — Nios II IIC, Nios V RISC-V CLINT, and ARM GICv1](explanation_phase4_interrupt_subsystems.md)
- **Tutorials:**
  - [Multi-Version Quartus Support](tutorial_phase11_multi_version_quartus.md)
  - [04b — Nios V/m LED Demo](../04b_niosv_led/doc/README.md)
  - [06b — Nios V/m Interrupt Demo](../06b_niosv_interrupts/doc/README.md)
  - [08b — Nios V/m Software Debugging with RISC-V GDB](../08b_niosv_debug/doc/README.md)
