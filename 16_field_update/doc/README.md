# Phase 10: Network Field Update & Automatic Rollback (No JTAG)

## Overview

In deployed FPGA+SoC systems, field engineers cannot connect a JTAG cable or physically swap microSD cards to upgrade firmware. The board must:

1. **Boot immediately on power-up** into its active firmware slot (`/opt/firmware/active` → `slot_a` or `slot_b`), programming the Cyclone V FPGA fabric and starting the HPS application automatically.
2. **Accept over-the-air (OTA) update bundles over Ethernet** (`ota_daemon.py` on TCP/HTTP port `8090`), hot-swapping both the FPGA bitstream (`de10_nano.rbf`) and the ARM application (`led_server`) without JTAG and without rebooting Linux.
3. **Automatically roll back** to the known-good slot if a network transfer is corrupted (SHA-256 mismatch), if the new FPGA bitstream fails configuration (`CONF_DONE` / `CRC_ERROR`), if the new application crashes or fails its post-start UDP health probe, or if power is lost mid-update.

---

## Project Structure

```text
16_field_update/
├── Makefile                        — Build bundles, run local tests, push OTA updates
├── doc/
│   └── README.md                   — This tutorial walkthrough
├── software/
│   └── led_server/
│       ├── Makefile                — Builds v1.0.0 (0x0F), v2.0.0 (0xA5), and crash variants
│       └── led_server.c            — Version-aware UDP LED server with startup pattern
├── daemon/
│   ├── ota_daemon.py               — On-board A/B slot manager, health checker & rollback engine
│   └── test_ota_daemon.py          — Automated unit & fault-injection test suite
├── client/
│   ├── pack_bundle.py              — Packages .rbf + led_server + manifest.json (.tar.gz)
│   └── ota_client.py               — PC CLI for status queries and OTA bundle uploads
└── br2-external/                   — Buildroot config with S30fpga_load, S40led_server, S50ota_daemon
```

---

## Architecture & Dual-Slot Layout

The persistent `ext4` root filesystem stores two independent firmware slots under `/opt/firmware/`:

```text
/opt/firmware/
├── state.json              # {"active_slot": "slot_a", "pending_slot": null, "boot_count": 1, ...}
├── active -> slot_a        # Atomic symlink to the currently active slot
├── slot_a/                 # Factory Golden v1.0.0 (startup LED pattern: 0x0F)
│   ├── manifest.json
│   ├── de10_nano.rbf
│   └── led_server
└── slot_b/                 # Standby slot (populated by network update, e.g. v2.0.0 LED=0xA5)
    ├── manifest.json
    ├── de10_nano.rbf
    └── led_server
```

### Safe Live FPGA Reconfiguration (Bridge Gating)

Before reprogramming the Cyclone V FPGA at runtime, [`ota_daemon.py`](../daemon/ota_daemon.py) stops `led_server` and [`fpga_load.c`](../../10_linux_led/software/fpga_load/fpga_load.c) asserts `BRGMODRST_LWH2F | BRGMODRST_H2F` (`0xFFD0501C`) to fence the AXI bridges before pulsing `NCFGPULL`. Once the new compressed `.rbf` reaches `STATE_USER_MODE` (`0x4`), `fpga_load` releases the bridge resets and re-enables L3 remap.

---

## Quick Start: Local Simulation & Fault-Injection Tests (No Board Required)

You can verify the complete Part 1 (happy-path update) and Part 3 (all 4 fault-injection & rollback scenarios) on your Linux PC before touching hardware:

```bash
cd 16_field_update
make test
```

Expected output:

```text
test_01_part1_happy_path_live_update ... ok
test_02_part3_fault_a_corrupted_transfer_rejected ... ok
test_03_part3_fault_b_bad_bitstream_rolls_back ... ok
test_04_part3_fault_c_crashing_app_rolls_back ... ok
test_05_part3_boot_guard_power_loss_recovery ... ok
----------------------------------------------------------------------
Ran 5 tests in 2.1s
OK
```

---

## Part 1: Auto-Boot & Live Network Update on Hardware

### Step 1: Build the OTA Update Bundles

```bash
cd 16_field_update
make bundles
```

This builds five `.tar.gz` bundles in `build/`:
- `build/fw_v1.0.0.tar.gz` — Golden `v1.0.0` (`de10_nano.rbf` + `led_server` with startup LED pattern `0x0F`)
- `build/fw_v2.0.0.tar.gz` — Updated `v2.0.0` (`de10_nano.rbf` + `led_server` with startup LED pattern `0xA5`)
- `build/fw_corrupt.tar.gz` — Corrupted payload (Fault A)
- `build/fw_bad_rbf.tar.gz` — Uncompressed/invalid `.rbf` bitstream (Fault B)
- `build/fw_crash_app.tar.gz` — Crashing `led_server` binary (Fault C)

### Step 2: Query Current Board Status

```bash
make status HOST=192.168.1.100
# Or over link-local IPv6:
make status HOST=fe80::2833:8aff:fe95:cb3d%eth0
```

At initial power-up, the board runs `slot_a` (`v1.0.0`), and the lower 4 LEDs (`0x0F`) are lit.

### Step 3: Push `v2.0.0` Over the Network (Zero JTAG)

```bash
make push-v2 HOST=192.168.1.100
```

The board unpacks `fw_v2.0.0.tar.gz`, verifies the SHA-256 checksums, stages the files into `slot_b`, reprograms the FPGA via `fpga_load`, starts `slot_b/led_server`, verifies the UDP health check on port `5005`, and atomically commits `active -> slot_b`. The board LEDs immediately switch to `0xA5`.

---

## Part 3: Fault Injection & Automatic Rollback

### Fault Scenario A: Corrupted Network Transfer (`SHA-256` Mismatch)

```bash
make test-corrupt-transfer HOST=192.168.1.100
```

`pack_bundle.py --corrupt-payload` flips a byte in `de10_nano.rbf` after generating `manifest.json`. `ota_daemon` catches the SHA-256 mismatch in `/tmp/ota_staging/` and rejects the update before stopping the running application (**zero downtime**).

### Fault Scenario B: Bad / Uncompressed FPGA Bitstream (`CONF_DONE` Timeout)

```bash
make test-bad-bitstream HOST=192.168.1.100
```

`pack_bundle.py --bad-rbf` packages an invalid/uncompressed `.rbf` with a valid SHA-256 in `manifest.json`. When `fpga_load` fails to reach `CONF_DONE`, `ota_daemon` automatically reloads the previous slot's working `.rbf`, restarts the previous slot's `led_server`, verifies its UDP health probe, and reports `status: rolled_back`.

### Fault Scenario C: Crashing Application Firmware (`UDP Health Probe` Failure)

```bash
make test-bad-app HOST=192.168.1.100
```

`fw_crash_app.tar.gz` contains a valid `.rbf` and a valid SHA-256, but its `led_server` binary exits immediately with a startup fault. `ota_daemon` detects the failed startup / UDP health probe, reverts `/opt/firmware/active` to the previous known-good slot, reloads its `.rbf` and `led_server`, and confirms recovery.
