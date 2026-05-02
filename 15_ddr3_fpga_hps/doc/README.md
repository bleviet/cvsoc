# Tutorial 15 — Shared DDR3 Access from FPGA and HPS

> **Phase 3B** | **Prerequisite:** [Tutorial 14 — DDR3 HPS Test](../../14_ddr3_hps_test/doc/README.md) | **Board:** DE10-Nano

## Overview

This tutorial enables the **FPGA-to-HPS SDRAM bridge** (`f2h_sdram0`) so FPGA logic can directly read/write DDR3 memory in parallel with the ARM CPU. A custom Avalon-MM master in the FPGA fabric performs DDR3 transfers independently, while the HPS controls it via register access over the Lightweight bridge.

### What You Will Learn

- How the F2H SDRAM bridge connects FPGA logic to the HPS DDR3 controller
- Designing a custom Avalon-MM master in VHDL
- Platform Designer integration of a custom component
- Shared memory patterns: producer-consumer with cross-verification
- Cache coherency considerations for FPGA/HPS shared memory

## Architecture

```
┌───────────────────────────────────────────┐
│              ARM Cortex-A9                │
│                                           │
│  main.c                                   │
│    ├── HPS writes region A                │
│    ├── Starts FPGA engine (region B)      │
│    ├── HPS verifies region B              │
│    └── FPGA verifies region A             │
│                                           │
│  ┌─────────┐  ┌──────────────────────┐    │
│  │ DDR3    │  │ LW H2F Bridge        │    │
│  │ Ctrl    │  │ 0xFF200000           │    │
│  │         │  │  ├── LED PIO  @+0x0  │    │
│  │         │  │  └── DDR3 Ctrl @+0x1000   │
│  │         │  │      (slave i/f)     │    │
│  └────┬────┘  └──────────────────────┘    │
│       │              │                    │
│  F2H SDRAM      Avalon Master             │
│  Bridge         (ddr3_test_master)        │
│  (64-bit)       reads/writes DDR3         │
│       │              │                    │
│  HPS Hard Block      │                    │
└───────┼──────────────┘────────────────────┘
        │
  ┌─────▼─────┐
  │ DDR3 SDRAM│  ← accessed by BOTH HPS and FPGA
  │ (1 GB)    │
  └───────────┘
```

## Register Map — ddr3_test_master

The FPGA test engine is controlled from the HPS via registers at `0xFF201000`:

| Offset | Name | Access | Description |
|--------|------|--------|-------------|
| `0x00` | CTRL | R/W | bit 0: start (write 1 to begin) |
| | | | bit 1: mode (0=write, 1=read+verify) |
| | | | bit 2: running (read-only) |
| `0x04` | BASE_ADDR | R/W | Start address in DDR3 space |
| `0x08` | LENGTH | R/W | Number of 32-bit words to transfer |
| `0x0C` | STATUS | R/O | bit 0: done |
| | | | bit 1: error (any mismatch) |
| | | | bits [31:16]: error count |
| `0x10` | PATTERN | R/W | Base test pattern (actual data = pattern + offset) |

## Test Procedure

```
┌─────────┐                                ┌──────────────┐
│   HPS   │                                │ FPGA Master  │
│ (ARM)   │                                │ (VHDL FSM)   │
└────┬────┘                                └──────┬───────┘
     │                                            │
     │ Phase 1: Write pattern A to region A       │
     ├────────────────────────────────────────→ (idle)
     │                                            │
     │ Phase 2: Start FPGA write (region B)       │
     ├────────────────────────────────────────→ Write B
     │ (wait for done)                            │
     │←────────────────────────────────────────── done
     │                                            │
     │ Phase 3: HPS reads region B, verifies      │
     ├────────────────────────────────────────→ (idle)
     │                                            │
     │ Phase 4: Start FPGA read+verify (region A) │
     ├────────────────────────────────────────→ Read A
     │ (wait for done, check errors)              │
     │←────────────────────────────────────────── done
     │                                            │
     │ ALL PASS → LEDs all on                     │
     ▼                                            ▼
```

## LED Feedback

| LED Pattern | Meaning |
|-------------|---------|
| `0x01` (LED 0) | Phase 1: HPS writing region A |
| `0x03` (LED 0,1) | Phase 2: FPGA writing region B |
| `0x07` (LED 0,1,2) | Phase 3: HPS verifying region B |
| `0x0F` (LED 0-3) | Phase 4: FPGA verifying region A |
| `0xFF` (all on) | **All tests passed** |
| Blinking | **Test failed** (phase shown by pattern) |

## How to Build

```bash
cd 15_ddr3_fpga_hps/quartus

# Full build: Qsys (with custom component) → Quartus → ARM app
make all
```

## How to Run

```bash
# Step 1: Program the FPGA bitstream
make program-sof

# Step 2: Load and run the ARM binary (DDR3 must be initialised by SPL)
make download-elf
```

## Key Design Decisions

### F2H SDRAM Bridge Width
We use 64-bit width (`F2S_Width=2`) for a good balance between bandwidth and resource usage. The bridge supports 32, 64, or 128 bits.

### Sequential vs. Burst Transfers
The `ddr3_test_master` uses sequential (non-burst) 32-bit transfers for simplicity. Each transaction is a single-word read or write. A burst-capable version would be faster but adds FSM complexity.

### Separate Test Regions
HPS and FPGA write to non-overlapping DDR3 regions to avoid race conditions. Cross-verification (HPS reads FPGA's region and vice versa) proves both paths work correctly.

### Cache Coherency
The HPS Cortex-A9 has L1/L2 caches. When the FPGA writes to DDR3 via the F2H SDRAM bridge, those writes bypass the cache. For this bare-metal test, caches are disabled (default after SPL), so coherency is not an issue. In a cached system, you would need cache invalidation before reading FPGA-written data.

## Files

| File | Description |
|------|-------------|
| `qsys/hps_system.tcl` | Platform Designer system with F2H SDRAM bridge |
| `qsys/ddr3_test_master_hw.tcl` | Custom component descriptor |
| `hdl/ddr3_test_master.vhd` | FPGA DDR3 test engine (Avalon-MM master) |
| `hdl/de10_nano_top.vhd` | VHDL top-level wrapper |
| `software/app/main.c` | HPS test orchestrator |
| `software/app/startup.S` | ARM startup code |
| `software/app/linker.ld` | Linker script |
| `quartus/Makefile` | Build orchestration |
