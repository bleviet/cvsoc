# Phase 3B — DDR3 Memory

> **Prerequisites:** [Phase 3 — HPS Bare Metal](tutorial_phase3_hps_baremetal.md)

This phase teaches DDR3 SDRAM usage on the Cyclone V SoC through two progressively complex tutorials.

---

## Background: DDR3 on the Cyclone V SoC

The DE10-Nano has **1 GB DDR3L SDRAM** (two Micron MT41K256M16HA chips) connected exclusively to the **HPS DDR3 controller** — a hard block inside the HPS subsystem. This is a critical architectural fact:

> **The FPGA fabric has no direct connection to DDR3.** All DDR3 access — whether from the ARM CPU or from FPGA logic — goes through the HPS DDR3 controller.

### Memory Map

From the ARM Cortex-A9's perspective:

| Address Range | Size | Description |
|---------------|------|-------------|
| `0x00000000` – `0x3FFFFFFF` | 1 GB | DDR3 SDRAM |
| `0xC0000000` – `0xFBFFFFFF` | 960 MB | HPS-to-FPGA bridge window |
| `0xFF200000` – `0xFF3FFFFF` | 2 MB | Lightweight HPS-to-FPGA bridge |
| `0xFFFF0000` – `0xFFFFFFFF` | 64 KB | HPS On-Chip RAM (OCRAM) |

### The Three Bridges

The Cyclone V SoC provides three bridges between HPS and FPGA:

```
                    ┌─────────────────────────────┐
                    │        ARM Cortex-A9         │
                    │                              │
                    │  ┌────────┐  ┌──────────┐    │
                    │  │ DDR3   │  │ L3       │    │
                    │  │ Ctrl   │  │ Intercon. │   │
                    │  └───┬────┘  └──┬───┬───┘    │
                    │      │         │   │         │
                    │ HPS Hard Block │   │         │
                    └──────┼─────────┼───┼─────────┘
                           │         │   │
              ┌────────────┘    ┌────┘   └────┐
              │                 │              │
         F2H SDRAM        H2F Bridge    LW H2F Bridge
         Bridge            (heavy)      (lightweight)
         (FPGA→DDR3)      (HPS→FPGA)   (HPS→FPGA regs)
              │                 │              │
              ▼                 ▼              ▼
         ┌─────────────────────────────────────────┐
         │              FPGA Fabric                 │
         └─────────────────────────────────────────┘
```

| Bridge | Direction | Width | Use Case |
|--------|-----------|-------|----------|
| **LW H2F** | HPS → FPGA | 32-bit | Low-bandwidth register access (PIOs, CSRs) |
| **H2F (heavy)** | HPS → FPGA | 32/64/128-bit | High-bandwidth data push to FPGA |
| **F2H SDRAM** | FPGA → HPS DDR3 | 32/64/128-bit | FPGA reads/writes DDR3 via HPS controller |

---

## Tutorial 14 — DDR3 Memory Test from HPS

**Directory:** `14_ddr3_hps_test/`

This tutorial validates DDR3 integrity using three standard memory test patterns, all executed from the ARM Cortex-A9. The FPGA design is identical to `05_hps_led` — no new hardware is needed.

### Why This Matters

Before using DDR3 for any real application, you need confidence that every bit in every address is working. Memory faults can be caused by:
- Manufacturing defects in the DDR3 chips
- Solder joint failures on the PCB
- Signal integrity issues (crosstalk, impedance mismatch)
- DDR3 timing parameter misconfiguration

### Key Difference from 05_hps_led

In `05_hps_led`, the application ran from the 64 KB **On-Chip RAM** (OCRAM) at `0xFFFF0000`. This tutorial runs from **DDR3** at `0x00100000`, which means:

1. **The DDR3 controller must be initialised first** — U-Boot SPL does this during the normal boot sequence
2. **The linker script targets DDR3** — `ORIGIN = 0x00100000` instead of `0xFFFF0000`
3. **We have much more memory** — the test region spans 62 MB

### Test Patterns Explained

#### 1. Walking Ones
```
Address     Write      Read back   Purpose
0x00200000  0x00000001 0x00000001  ✓  bit 0 works
0x00200000  0x00000002 0x00000002  ✓  bit 1 works
0x00200000  0x00000004 0x00000004  ✓  bit 2 works
...
0x00200000  0x80000000 0x80000000  ✓  bit 31 works
(repeat at next page)
```

Detects **stuck-at faults**: if data line D5 is shorted to ground, `0x00000020` would read back as `0x00000000`.

#### 2. Address-as-Data
```
Address     Write      Read back
0x00200000  0x00200000 0x00200000  ✓
0x00200004  0x00200004 0x00200004  ✓
0x00200008  0x00200008 0x00200008  ✓
...
```

Detects **address line faults**: if address line A10 is floating, two different addresses would alias to the same physical cell.

#### 3. Alternating Patterns
```
Pass 1: Fill with 0xAAAAAAAA (10101010...)
Pass 2: Fill with 0x55555555 (01010101...)
```

Detects **coupling faults**: adjacent data lines influencing each other.

### Build and Run

```bash
cd 14_ddr3_hps_test/quartus
make all              # FPGA bitstream + ARM app
make program-sof      # program FPGA
make download-elf     # load app into DDR3 via JTAG
```

---

## Tutorial 15 — Shared DDR3 Access from FPGA and HPS

**Directory:** `15_ddr3_fpga_hps/`

This tutorial enables the **FPGA-to-HPS SDRAM bridge** so FPGA logic can read/write DDR3 memory concurrently with the ARM CPU.

### The F2H SDRAM Bridge

The F2H SDRAM bridge is a hard AXI port that connects FPGA fabric Avalon-MM masters directly to the HPS DDR3 controller. From the FPGA side, DDR3 appears as a flat address space starting at `0x00000000`.

To enable it in Platform Designer:
```tcl
# Width: 0=disabled, 1=32-bit, 2=64-bit, 3=128-bit
set_instance_parameter_value hps_0 F2S_Width {2}
```

The bridge must be clocked:
```tcl
add_connection clk_0.clk hps_0.f2h_sdram0_clock
```

### Custom Avalon-MM Master Design

The `ddr3_test_master` is a VHDL component with two interfaces:

1. **Slave interface** (on the LW bridge): 5 control/status registers that the HPS uses to configure and monitor the test engine
2. **Master interface** (on the F2H SDRAM bridge): issues sequential read/write transactions to DDR3

The FSM is straightforward:
```
IDLE ──(start=1)──→ WRITE/READ ──(all words done)──→ DONE ──(start cleared)──→ IDLE
```

### Platform Designer Integration

Custom components need a `_hw.tcl` descriptor file:
```tcl
# ddr3_test_master_hw.tcl
set_module_property NAME ddr3_test_master
add_fileset QUARTUS_SYNTH ...
add_interface avs avalon end      # slave (control regs)
add_interface avm avalon start    # master (DDR3 access)
```

### Producer-Consumer Test

The test proves concurrent DDR3 access works by having HPS and FPGA each write to separate regions, then cross-verify:

| Phase | Actor | Action | Region |
|-------|-------|--------|--------|
| 1 | HPS | Write pattern `0xCAFE0000 + i` | A (`0x00200000`, 1 MB) |
| 2 | FPGA | Write pattern `0xBEEF0000 + i` | B (`0x03000000`, 1 MB) |
| 3 | HPS | Read region B, verify FPGA's data | B |
| 4 | FPGA | Read region A, verify HPS's data | A |

If all four phases pass, both data paths are verified.

### Cache Coherency Note

In this bare-metal context, the Cortex-A9 caches are disabled (the default after SPL when no OS sets them up). This means HPS reads see FPGA writes immediately — no cache invalidation needed.

In a **Linux** or **Zephyr** context where caches are enabled, you would need to:
- Use uncached memory mappings (e.g., `ioremap_nocache()` in Linux)
- Or explicitly invalidate/flush caches before/after shared memory access

### Build and Run

```bash
cd 15_ddr3_fpga_hps/quartus
make all              # Qsys (custom component) + FPGA + ARM app
make program-sof      # program FPGA
make download-elf     # load app into DDR3 via JTAG
```

---

## Troubleshooting

### "Data Abort" when accessing DDR3
- **Cause:** DDR3 controller not initialised. The SPL must run first.
- **Fix:** Boot the board to U-Boot, then use `make download-elf` to load the test app.

### All LEDs blink (test failure)
- **Cause:** Memory fault detected.
- **Which test failed:** The blinking LED pattern indicates the failed test:
  - `0x01` = Walking ones
  - `0x02` = Address-as-data
  - `0x04` = Alternating patterns
  - `0x07` / `0x0F` (tutorial 15) = Cross-verification failure

### FPGA test master never completes (tutorial 15)
- **Cause:** F2H SDRAM bridge not properly reset, or bridge clock not connected.
- **Fix:** Verify `hps_bridge_init()` releases the FPGA-to-HPS bridge reset. Check that `f2h_sdram0_clock` is connected in the Qsys TCL.

### Fitter Error 174068
- **Cause:** The DDR3 PHY OCT patch was not applied.
- **Fix:** Run `make patch-oct` or ensure the `all` target runs `patch_oct.py`.
