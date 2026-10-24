# How-to: Create, Test, and Troubleshoot OTA Firmware Bundles and Automatic Rollbacks

> **Series:** cvsoc — Stepping into advanced FPGA development on the DE10-Nano  
> **Type:** How-to Guide (Problem-oriented)  
> **Difficulty:** Intermediate–Advanced  
> **Applies to:** Phase 10 (`16_field_update` — [`16_field_update/doc/README.md`](../16_field_update/doc/README.md))

---

## Overview

In Phase 10 (`16_field_update`), the DE10-Nano runs an Over-The-Air (OTA) update daemon (`ota_daemon.py`) on HTTP port `8090` that manages two atomic firmware slots (`/opt/firmware/slot_a` and `/opt/firmware/slot_b`). Each slot holds a matched pair of hardware (`de10_nano.rbf`) and software (`led_server`) plus a cryptographic checksum manifest (`manifest.json`).

Use this guide when you want to:
1. Package your own custom FPGA bitstream (`.rbf`) and ARM binary into a signed/hashed `.tar.gz` OTA bundle.
2. Simulate and test your OTA bundle (including fault-injection rollback scenarios) locally on your PC without touching hardware.
3. Push a live zero-JTAG update to a running DE10-Nano over IPv4 or IPv6.
4. Manually inspect or recover slot state if a board is in a degraded state.

---

## Recipe 1: Package a Custom `.rbf` and Application into an OTA Bundle

Every OTA bundle is a `.tar.gz` archive containing three top-level files:
- `de10_nano.rbf` (compressed FPGA bitstream)
- `led_server` (cross-compiled ARM ELF executable)
- `manifest.json` (version metadata, file byte sizes, and SHA-256 digests)

Use [`16_field_update/client/pack_bundle.py`](../16_field_update/client/pack_bundle.py) to create a bundle from any `.rbf` and `led_server` binary:

```bash
cd 16_field_update

python3 client/pack_bundle.py \
  --version "2.1.0" \
  --rbf /path/to/your/de10_nano.rbf \
  --app /path/to/your/led_server \
  --output build/fw_v2.1.0.tar.gz \
  --notes "Custom PWM FPGA bitstream + updated LED server"
```

### Inspect the Generated Bundle Manifest

Verify the contents and SHA-256 digests inside your `.tar.gz` archive before uploading:

```bash
tar -ztvf build/fw_v2.1.0.tar.gz
tar -zxvf build/fw_v2.1.0.tar.gz -O manifest.json
```

Expected output:
```json
{
  "schema_version": 1,
  "version": "2.1.0",
  "notes": "Custom PWM FPGA bitstream + updated LED server",
  "files": {
    "de10_nano.rbf": {
      "sha256": "...",
      "size": 1954012
    },
    "led_server": {
      "sha256": "...",
      "size": 14820
    }
  }
}
```

---

## Recipe 2: Test Your Bundle and Rollback Paths Locally (No Board Required)

Before pushing an update to a remote board, run the local simulation suite in `16_field_update/`:

```bash
cd 16_field_update
make test
```

### How to Generate Fault-Injected Bundles for Testing Rollback Logic

`pack_bundle.py` includes built-in fault-injection flags so you can verify that `ota_daemon.py` catches failures at every stage:

1. **Simulate Network/Storage Corruption (`--corrupt-payload`):**
   Flips bytes inside `de10_nano.rbf` *after* computing `manifest.json` so Gate 1 (SHA-256 verification) rejects the bundle in `/tmp/ota_staging/` with zero downtime:
   ```bash
   python3 client/pack_bundle.py \
     --version "2.0.0-corrupt" \
     --rbf de10_nano.rbf \
     --app software/led_server/led_server_v2 \
     --corrupt-payload \
     --output build/fw_corrupt.tar.gz
   ```

2. **Simulate an Uncompressed/Bad FPGA Bitstream (`--bad-rbf`):**
   Creates a payload with a valid `manifest.json` SHA-256, but whose `de10_nano.rbf` fails FPGA Manager configuration (`CONF_DONE` timeout / `nSTATUS` error), triggering Gate 2 automatic rollback:
   ```bash
   python3 client/pack_bundle.py \
     --version "2.0.0-badrbf" \
     --rbf de10_nano.rbf \
     --app software/led_server/led_server_v2 \
     --bad-rbf \
     --output build/fw_bad_rbf.tar.gz
   ```

3. **Simulate a Crashing Application Binary (`led_server_crash`):**
   Uses a valid `.rbf` and valid SHA-256, but packages a binary compiled with `-DCRASH_ON_START=1` (`exit(1)` before binding UDP port `5005`), triggering Gate 3 (`udp_health_probe` failure) automatic rollback:
   ```bash
   python3 client/pack_bundle.py \
     --version "2.0.0-crash" \
     --rbf de10_nano.rbf \
     --app software/led_server/led_server_crash \
     --output build/fw_crash_app.tar.gz
   ```

---

## Recipe 3: Push an OTA Update and Query Status Over the Network

### 1. Check the board's current active slot, firmware version, and LED pattern

```bash
cd 16_field_update

# Using IPv4 or Link-Local IPv6 (with %interface zone ID):
python3 client/ota_client.py --host "fe80::2833:8aff:fe95:cb3d%enx08beac224c03" status
```

### 2. Push your new `.tar.gz` bundle

```bash
python3 client/ota_client.py \
  --host "fe80::2833:8aff:fe95:cb3d%enx08beac224c03" \
  push build/fw_v2.1.0.tar.gz
```

- If the update succeeds, `ota_client.py` prints `Result: committed`, `Active slot: slot_b (v2.1.0)`, and the live hardware LED read-back pattern.
- If the new `.rbf` or `led_server` fails health checks, `ota_client.py` prints `Result: rolled_back`, the exact failure reason (`fpga_load` or `UDP health probe`), and confirms that `slot_a` (`v1.0.0`) has been restored and verified.

---

## Recipe 4: Trigger Boot-Guard Recovery or Inspect `/opt/firmware/` on the Board

If power was lost mid-update, `S50ota_daemon` automatically runs `boot_guard_check()` on boot. You can also trigger recovery manually over HTTP or inspect the on-board state file via SSH:

```bash
# Trigger boot-guard check remotely over HTTP:
python3 client/ota_client.py --host "fe80::2833:8aff:fe95:cb3d%enx08beac224c03" recover

# Or inspect /opt/firmware/ directly on the board:
ssh "root@fe80::2833:8aff:fe95:cb3d%enx08beac224c03" \
  "ls -la /opt/firmware && cat /opt/firmware/state.json"
```

---

## Related Documentation

- **Explanation:**
  - [Explanation: Atomic A/B Field Updates, Live FPGA Hot-Swap, and Automatic Rollback](explanation_phase10_ota_field_update_and_rollback.md)
- **Tutorial:**
  - [Phase 10: Network Field Update & Automatic Rollback (No JTAG)](../16_field_update/doc/README.md)
