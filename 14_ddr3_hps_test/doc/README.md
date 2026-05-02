# Tutorial 14 — DDR3 Memory Test from HPS

> **Phase 3B** | **Prerequisite:** [Tutorial 05 — HPS LED](../../docs/tutorial_phase3_hps_baremetal.md) | **Board:** DE10-Nano

## Overview

This tutorial validates the DDR3 SDRAM on the DE10-Nano board by running three memory test patterns from the ARM Cortex-A9 HPS. It reuses the same FPGA design as `05_hps_led` — the DDR3 controller is part of the HPS hard block and requires no additional Qsys components.

### What You Will Learn

- How the Cyclone V HPS DDR3 SDRAM controller works
- The HPS memory map (DDR3 at `0x00000000`–`0x3FFFFFFF`)
- Standard memory test patterns and their purpose
- Running bare-metal applications from DDR3 instead of OCRAM

## Architecture

```
┌───────────────────────────────────┐
│           ARM Cortex-A9           │
│                                   │
│  DDR3 Test App (main.c)           │
│    ├── Walking ones test          │
│    ├── Address-as-data test       │
│    └── Alternating patterns test  │
│                                   │
│  ┌─────────┐  ┌───────────────┐   │
│  │ DDR3    │  │ LW H2F Bridge │   │
│  │ Ctrl    │  │ (0xFF200000)  │   │
│  └────┬────┘  └──────┬────────┘   │
│       │              │            │
│ HPS Hard Block       │            │
└───────┼──────────────┼────────────┘
        │              │
  ┌─────▼─────┐  ┌────▼────┐
  │ DDR3 SDRAM│  │ LED PIO │
  │ (1 GB)    │  │ (8-bit) │
  └───────────┘  └─────────┘
```

## Memory Map

| Region | Address Range | Size | Purpose |
|--------|--------------|------|---------|
| SPL / U-Boot workspace | `0x00000000` – `0x000FFFFF` | 1 MB | Reserved — do not touch |
| Application code+stack | `0x00100000` – `0x001FFFFF` | 1 MB | This binary (startup.S + main.c) |
| DDR3 test region | `0x00200000` – `0x03FFFFFF` | 62 MB | Written/read by test patterns |

## Test Patterns

### 1. Walking Ones
A single `1` bit walks through each of the 32 bit positions at sampled addresses (one per 4 KB page). Detects **stuck-at faults** on individual data lines.

### 2. Address-as-Data
Writes each address's own value as its data word, then reads everything back in a second pass. Detects **address line faults** (solder bridges, open pins) and data retention issues.

### 3. Alternating Patterns
Fills the test region with `0xAAAAAAAA`, reads back, then fills with `0x55555555` and reads back. Detects **coupling faults** between adjacent data lines.

## LED Feedback

| Pattern | Meaning |
|---------|---------|
| Running light (shifting single LED) | Test in progress |
| Upper LEDs indicate phase (7=test 1, 7+6=test 2, etc.) | Which test is running |
| All 8 LEDs on | **All tests passed** |
| Blinking pattern | **Test failed** (pattern shows which test) |

## How to Build

```bash
cd 14_ddr3_hps_test/quartus

# Full build: Qsys → Quartus → ARM app
make all
```

## How to Run

> **Important:** The DDR3 controller must be initialised before running this application.
> Boot the board to U-Boot first, then load via JTAG.

```bash
# Step 1: Program the FPGA bitstream
make program-sof

# Step 2: Load and run the ARM binary (requires U-Boot to have initialised DDR3)
make download-elf
```

## Prerequisites

- DE10-Nano board with USB-Blaster connected
- `cvsoc/quartus:23.1` Docker image (see [setup guide](../../docs/tutorial_docker_dev_environment.md))
- ARM cross-compiler: `arm-linux-gnueabihf-gcc` (pre-installed in Docker image)

## Files

| File | Description |
|------|-------------|
| `qsys/hps_system.tcl` | Platform Designer system (same as 05_hps_led) |
| `hdl/de10_nano_top.vhd` | VHDL top-level wrapper |
| `software/app/main.c` | DDR3 memory test application |
| `software/app/startup.S` | ARM startup code (runs from DDR3) |
| `software/app/linker.ld` | Linker script (code at 0x00100000) |
| `quartus/Makefile` | Build orchestration |
