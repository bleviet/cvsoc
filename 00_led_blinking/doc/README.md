# LED Blinking Project

## Overview

This is the first project in the cvsoc series. It makes all eight LEDs on the DE10-Nano board blink independently at 1 Hz, implemented entirely in VHDL. The design demonstrates the complete FPGA development workflow: writing HDL, creating a Quartus project with a TCL script, compiling inside a Docker container, and programming the device via JTAG from WSL2.

## Project Structure

```
00_led_blinking/
├── doc/         — This documentation
├── hdl/
│   ├── de10_nano_top.vhd        — Top-level entity (clock, reset, 8× LED instances)
│   └── led_blinking.vhd         — LED blinking module (clock divider + toggle)
├── quartus/
│   ├── Makefile                 — Build, compile, and program automation
│   ├── de10_nano.sdc            — Timing constraints (50 MHz clock)
│   ├── de10_nano_pin_assignments.tcl — Physical pin constraints for DE10-Nano
│   └── de10_nano_project.tcl   — Quartus project creation script
└── scripts/
    └── check_timing_slacks.py  — Post-compile timing slack checker (shared by later phases)
```

## How to Build

This is a **pure HDL design** — no software component — and it builds with **both** Quartus
23.1 and 25.1. The `quartus/Makefile` includes the shared
`common/make/quartus-version.mk` fragment, which auto-detects a local Quartus 25.1 install
or falls back to the `cvsoc/quartus:23.1` Docker container. From the project's `quartus/`
directory:

```bash
cd 00_led_blinking/quartus

# Auto-detect: local 25.1 if installed, else Docker 23.1
make all

# Or force a specific version
make QUARTUS_VERSION=25.1 all
make QUARTUS_VERSION=23.1 all
```

Step by step:

```bash
# Create the Quartus project
make project

# Compile (synthesis + fit + assemble + STA)
make compile
```

## How to Program

The Makefile detects native Linux and WSL2 automatically. On native Linux, the
USB-Blaster is used directly. On WSL2, it is attached with `usbipd-win` before
programming. From the `quartus/` directory:

```bash
cd 00_led_blinking/quartus

# Native Linux (for example Ubuntu)
make program-sof

# WSL2 (replace 2-4 with your Windows USB bus ID)
make program-sof USBIPD_BUSID=2-4
```

Use `make usb-ready` to check the detected host-specific USB setup without
programming. `USB_HOST=linux` or `USB_HOST=wsl` can override auto-detection.

All eight LEDs will blink at 1 Hz after programming succeeds.

## See Also

- Full tutorial: [`docs/tutorial_phase0_led_blinking.md`](../../docs/tutorial_phase0_led_blinking.md)
- Docker environment: [`docs/tutorial_docker_dev_environment.md`](../../docs/tutorial_docker_dev_environment.md)
- Multi-version Quartus: [`docs/tutorial_phase11_multi_version_quartus.md`](../../docs/tutorial_phase11_multi_version_quartus.md)
