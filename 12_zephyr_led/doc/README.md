# 12 — Zephyr RTOS LED Demo on DE10-Nano HPS

Runs **Zephyr RTOS** on the ARM Cortex-A9 Hard Processor System (HPS) of
the Terasic DE10-Nano. Controls the 8 FPGA-fabric LEDs through the
Lightweight HPS-to-FPGA bridge at `0xFF200000` from two cooperative threads.

## Architecture

```
 DE10-Nano
 ┌──────────────────────────────────────────────────────────┐
 │ HPS (ARM Cortex-A9 @ 800 MHz — runs Zephyr RTOS)         │
 │                                                           │
 │  ┌─────────────────────┐   ┌─────────────────────────┐   │
 │  │  led_pattern_thread  │   │  button_monitor_thread   │   │
 │  │  prio 5 / 2 KB stack │   │  prio 4 / 1 KB stack     │   │
 │  │                      │   │                           │   │
 │  │  Cycles through 4    │   │  gpio_add_callback()      │   │
 │  │  LED animations via  │   │  on KEY[0] / KEY[1]       │   │
 │  │  sys_write32()       │   │  → k_sem_give()           │   │
 │  │  @ 120 ms/frame      │   │                           │   │
 │  └──────────┬───────────┘   └────────────┬──────────────┘   │
 │             │  LW H2F bridge              │ HPS GPIO         │
 └─────────────┼─────────────────────────────┼──────────────────┘
               │ 0xFF200000                  │
               ▼                             ▼
        ┌─────────────┐             ┌──────────────┐
        │  FPGA LED   │             │  KEY[0]       │
        │  PIO (8 bit)│             │  KEY[1]       │
        │  LED[7:0]   │             │  (active-low) │
        └─────────────┘             └──────────────┘
```

### LED patterns

| Pattern | Effect | Bitmask sequence |
|---------|--------|-----------------|
| `CHASE` | Single LED sweeps left→right | `0x01 → 0x02 → 0x04 → … → 0x80 → 0x01` |
| `BREATHE` | LEDs fill up then drain | `0x01 → 0x03 → … → 0xFF → 0x7F → … → 0x00` |
| `BLINK` | All LEDs on / off | `0xFF → 0x00 → 0xFF → …` |
| `STRIPES` | Checkerboard alternate | `0xAA → 0x55 → 0xAA → …` |

Press **KEY[0]** or **KEY[1]** to advance to the next pattern.

---

## Prerequisites

| Requirement | Notes |
|---|---|
| DE10-Nano board | USB-Blaster II connected to host |
| Zephyr SDK 1.0.1 | Install to `$HOME` per `docs/INSTALL_DEPS_PHASE8.md` |
| `west` 1.x | `pip3 install --user west` |
| Zephyr `main` | Checked out by `make init` |
| Python ≥ 3.12 | `apt-get install python3` |
| CMake ≥ 3.20 | `apt-get install cmake` |
| Ninja | `apt-get install ninja-build` |

See [docs/INSTALL_DEPS_PHASE8.md](../../docs/INSTALL_DEPS_PHASE8.md) for full install steps.

---

## Directory structure

```
12_zephyr_led/
├── Makefile                       ← build orchestration (west wrappers)
├── west.yml                       ← not used directly (app/west.yml is the manifest)
├── .gitignore                     ← ignores zephyr/, modules/, build/, .west/
├── app/
│   ├── west.yml                   ← west manifest: Zephyr main + imports
│   ├── CMakeLists.txt             ← Zephyr CMake entry point
│   ├── prj.conf                   ← Kconfig: GPIO, UART console, threads
│   ├── boards/
│   │   └── cyclonev_socdk.overlay ← DE10-Nano: FPGA LED PIO + KEY[0]/KEY[1]
│   └── src/
│       └── main.c                 ← multi-threaded Zephyr application
└── doc/
    └── README.md                  ← this file
```

---

## How to build

### 1. One-time workspace initialisation

```bash
# From 12_zephyr_led/
make init   # west init -l app/ && west update  (~1.5 GB download)
make deps   # pip3 install -r zephyr/scripts/requirements.txt
```

### 2. Build

```bash
export ZEPHYR_SDK_INSTALL_DIR=$HOME/zephyr-sdk-1.0.1
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr

make build
# or directly:
west build -b cyclonev_socdk --build-dir build app
```

Expected output:
```
[144/144] Linking C executable zephyr/zephyr.elf
Memory region   Used Size  Region Size  %age Used
           RAM:    1200 KB        1 GB      0.11%
```

### 3. Flash to DE10-Nano

> **Before flashing**, the FPGA bitstream from Phase 7 (or any bitstream with
> led_pio and button_pio at `0xFF200000`) must be loaded onto the FPGA.

```bash
make flash
# Requires: USB-Blaster II connected + Zephyr preloader in board support dir
```

The Zephyr OpenOCD runner loads the preloader then Zephyr ELF via JTAG.

### 4. Monitor UART output

Connect to the DE10-Nano UART (mini-USB J8 port, `ttyUSB0`, 115200 8N1):

```bash
minicom -D /dev/ttyUSB0 -b 115200
```

Expected output:
```
*** Booting Zephyr OS build v4.4.99 ***
Phase 8 — Zephyr RTOS LED Demo
Board: cyclonev_socdk
FPGA LED PIO: 0xFF200000
Threads started. Press KEY0 or KEY1 to change pattern.
[00:00:00.001,000] <inf> zephyr_led: LED pattern thread started
[00:00:00.001,000] <inf> zephyr_led: FPGA LED PIO at 0xFF200000
[00:00:00.001,000] <inf> zephyr_led: Button monitor thread started
[00:00:00.001,000] <inf> zephyr_led: KEY0 and KEY1 interrupts configured
```

---

## Zephyr vs bare-metal comparison

| Feature | Phase 5–7 (Bare-metal / Linux) | Phase 8 (Zephyr RTOS) |
|---|---|---|
| Threading | `pthread` (Linux) / none | `k_thread_create()` native RTOS threads |
| GPIO interrupts | `/sys/class/gpio` or GIC bare | `gpio_add_callback()` — driver abstraction |
| FPGA register access | `mmap(/dev/mem)` | `sys_write32()` — direct MMIO (no OS needed) |
| Timing | `nanosleep()` / busy-wait | `k_msleep()` — RTOS scheduler sleep |
| Boot | Linux u-boot → kernel → userspace | Preloader → Zephyr (no kernel needed) |
| Binary size | ~500 KB kernel + userspace | ~49 KB flat binary |
| Build system | Make + cross-gcc | CMake + Ninja via `west` |

---

## Key source files

| File | Purpose |
|---|---|
| [`app/src/main.c`](../app/src/main.c) | Application: threads, GPIO setup, LED patterns |
| [`app/boards/cyclonev_socdk.overlay`](../app/boards/cyclonev_socdk.overlay) | DTS overlay: FPGA LED PIO, KEY[0]/KEY[1] |
| [`app/prj.conf`](../app/prj.conf) | Kconfig: GPIO, console, multithreading |
| [`app/west.yml`](../app/west.yml) | West manifest: pins Zephyr to `main` |

---

## Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| `Could not find Zephyr-sdk compatible with version 0.16` | Zephyr version mismatch | Use Zephyr `main` (SDK 1.0 requires Zephyr ≥ 4.4.99+) |
| `Missing jsonschema dependency` | Python deps not installed for Zephyr main | `make deps` |
| `KEY0 GPIO device not ready` | DTS overlay issue | Check gpio1 is enabled in overlay |
| LEDs don't move | FPGA bitstream not loaded | Load the Phase 5/7 bitstream first |
| UART shows nothing | Wrong port or baud | Use `/dev/ttyUSB0` at 115200 8N1 |
