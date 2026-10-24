# How-to: Troubleshoot JTAG, USB-Blaster II, and `jtagd` Issues Across WSL2, Linux, and Docker

> **Series:** cvsoc — Stepping into advanced FPGA development on the DE10-Nano  
> **Type:** How-to Guide (Problem-oriented)  
> **Difficulty:** Beginner–Intermediate  
> **Applies to:** All projects using JTAG (`00`–`09`, `12`, `14`, `15`)

---

## Overview

Almost every hardware interaction during development—programming `.sof` bitstreams (`quartus_pgm`), downloading Nios II/V ELFs (`nios2-download`, `niosv-download`), opening JTAG UART consoles (`nios2-terminal`, `juart-terminal`), and running GDB servers (`nios2-gdb-server`, Ashling RiscFree, `openocd`)—goes through the DE10-Nano's on-board **USB-Blaster II** (`09fb:6010` / `09fb:6001`).

Use this guide when a `make program-sof`, `make download-elf`, `make gdb-server`, or `make openocd` command fails to detect or communicate with the board.

---

## Quick Diagnostic Checklist: Run This First

Before trying random fixes, run these three commands from your Linux or WSL2 terminal to pinpoint *which layer* is broken:

### 1. Does the Linux kernel see the physical USB device?

```bash
lsusb | grep -i -E "09fb|altera|blaster"
```

- **Healthy output (configured USB-Blaster II):**
  ```text
  Bus 001 Device 004: ID 09fb:6010 Altera Blaster
  ```
- **Unconfigured Cypress FX2LP output (needs firmware upload by `jtagd`):**
  ```text
  Bus 001 Device 003: ID 09fb:6810 Altera
  ```
- **No output at all:**
  - Confirm the mini-USB cable is plugged into **J13 (`USB Blaster II`)** on the DE10-Nano (next to the HDMI port), **not** J4 (`UART` next to the Ethernet port)!
  - Confirm the board's `5V` DC barrel jack is plugged in and the power LED is lit (USB alone does not power the Cyclone V).
  - If on **WSL2**, the device is still attached to Windows—go to [Problem 1](#problem-1-lsusb-is-empty-on-wsl2-device-not-attached).

### 2. Does `jtagconfig` see both JTAG TAPs (`@1` and `@2`)?

```bash
# Inside Docker (23.1):
docker run --rm --privileged -v /dev/bus/usb:/dev/bus/usb cvsoc/quartus:23.1 \
  bash -c 'jtagd && sleep 2 && jtagconfig'
```

- **Healthy output:**
  ```text
  1) DE-SoC [1-1]
    4BA00477   SOCVHPS
    02D020DD   5CSEBA6(.|ES)/5CSEMA6/..
  ```

---

## Problem 1: `lsusb` Is Empty on WSL2 (Device Not Attached)

### Symptom
`jtagconfig` prints `No JTAG hardware available`, and `lsusb` inside WSL2 does not list `09fb:6010` or `09fb:6810`.

### Solution

1. In a Windows terminal (or from WSL2 using `usbipd.exe`), find the `BUSID` of the USB-Blaster:
   ```bash
   usbipd.exe list
   ```
   Look for `09fb:6010` or `09fb:6810` (`USB-Blaster II` or `DE-SoC`). Note its `BUSID` (for example, `2-4`).

2. If the `STATE` column says `Not shared`, bind it once from an **Administrator PowerShell** on Windows:
   ```powershell
   usbipd bind --busid 2-4
   ```

3. Attach the device to WSL2 from your project directory:
   ```bash
   make usb-wsl USBIPD_BUSID=2-4
   ```
   *(Tip: Export `export USBIPD_BUSID=2-4` in your `~/.bashrc` so you never have to pass it on the command line again.)*

---

## Problem 2: Native Linux Reports `No JTAG hardware available` or `Unable to lock chain (Insufficient port permissions)`

### Symptom
On a native Ubuntu/Debian Linux host (especially when `QUARTUS_RUNNER=local` with Quartus 25.1), `lsusb` shows `09fb:6010`, but running `jtagconfig` or `make program-sof` as a non-root user reports:
```text
1) USB-Blaster variant [1-1]
  Unable to lock chain - Insufficient port permissions
```

### Cause
By default on Linux, `/dev/bus/usb/<bus>/<dev>` nodes are owned by `root:root` with mode `0664`. When `jtagd` runs as your normal user account (`--user $(id -u):$(id -g)`), the kernel denies write access to the USB device node.

### Solution: Install `udev` Rules for Altera USB-Blaster II

Create `/etc/udev/rules.d/51-usbblaster.rules` on your Linux host:

```bash
sudo tee /etc/udev/rules.d/51-usbblaster.rules >/dev/null <<'EOF'
# Intel / Altera USB-Blaster and USB-Blaster II (DE10-Nano)
SUBSYSTEM=="usb", ATTR{idVendor}=="09fb", ATTR{idProduct}=="6001", MODE="0666"
SUBSYSTEM=="usb", ATTR{idVendor}=="09fb", ATTR{idProduct}=="6002", MODE="0666"
SUBSYSTEM=="usb", ATTR{idVendor}=="09fb", ATTR{idProduct}=="6003", MODE="0666"
SUBSYSTEM=="usb", ATTR{idVendor}=="09fb", ATTR{idProduct}=="6010", MODE="0666"
SUBSYSTEM=="usb", ATTR{idVendor}=="09fb", ATTR{idProduct}=="6810", MODE="0666"
EOF

sudo udevadm control --reload-rules
sudo udevadm trigger
```

Unplug and replug the DE10-Nano USB cable, then re-test `jtagconfig`.

---

## Problem 3: `jtagconfig` Shows `Unable to read device chain - JTAG chain broken` or Only One Device

### Symptom
`jtagconfig` detects `1) DE-SoC [1-1]`, but underneath it says `Unable to read device chain` or `00000000`.

### Solution
1. **Check 5V Power:** The USB-Blaster II microcontroller (`U18` on the DE10-Nano) is powered by the USB cable, so `DE-SoC [1-1]` appears even when the board's main 5V power supply is unplugged! However, the Cyclone V FPGA and HPS JTAG TAPs require the 5V DC barrel jack to be powered.
2. **Wait for FX2LP Re-Enumeration on WSL2:** When `jtagd` first connects to a freshly powered DE10-Nano (`09fb:6810`), it downloads firmware into the Cypress FX2LP chip. The chip disconnects from USB and re-enumerates as `09fb:6010` (`DE-SoC`). Under WSL2, when a USB device disconnects and reconnects with a new Product ID, `usbipd` may drop the attachment!
   - Run `usbipd.exe list` again—if `09fb:6010` is now listed as `Shared` instead of `Attached`, run `make usb-wsl USBIPD_BUSID=<busid>` once more.

---

## Problem 4: `nios2-download` or `nios2-gdb-server` Tries to Run `.exe` Binaries and Fails

### Symptom
Inside Docker on WSL2, running `nios2-download` manually prints:
```text
nios2-download: line ...: quartus_pgm.exe: command not found
```

### Cause
The Nios II wrapper script checks `uname -r`, sees `microsoft-standard-WSL2`, assumes it is running in Cygwin on Windows, and appends `.exe` to tool names.

### Solution
Always mount `common/docker/uname_shim.sh` over `/usr/local/bin/uname` when invoking Nios II JTAG tools manually inside Docker (all Makefiles in this repo already do this):

```bash
docker run --rm --privileged \
  -v /dev/bus/usb:/dev/bus/usb \
  -v $(pwd):/work \
  -v $(pwd)/common/docker/uname_shim.sh:/usr/local/bin/uname:ro \
  cvsoc/quartus:23.1 \
  bash -c 'jtagd && sleep 2 && nios2-download -g /work/04_nios2_led/software/app/nios2_led.elf'
```

---

## Problem 5: `Address already in use` When Starting `make gdb-server` or `make openocd`

### Symptom
Starting a debug session fails with:
```text
Bind for 0.0.0.0:2345 failed: port is already allocated
```
or port `3333` is already in use.

### Solution
A previous container or local GDB server is still holding the TCP port (or the JTAG cable). Stop it using the built-in Makefile target:

```bash
# In 06_nios2_interrupts/quartus, 08_nios2_debug/quartus, or 08b_niosv_debug/quartus:
make close-gdb-port

# Or stop any running cvsoc containers directly:
docker ps --filter "ancestor=cvsoc/quartus:23.1" -q | xargs -r docker stop
```

If running in `local` mode (Quartus 25.1 on the host), terminate stale `ash-riscv-gdb-server` or `jtagd` processes:
```bash
pkill -f ash-riscv-gdb-server || true
pkill -f nios2-gdb-server || true
```

---

## Related Documentation

- **Explanations:**
  - [Explanation: Hardware JTAG Debug Architecture — Nios II, Nios V RISC-V, and ARM CoreSight DAP](explanation_phase5_jtag_and_gdb_debugging.md)
  - [Explanation: Containerized FPGA Toolchains and Multi-Version Quartus Architecture](explanation_docker_and_multi_version_quartus.md)
- **Other How-To Guides:**
  - [How-to: Program the DE10-Nano from Windows When Working in WSL2](howto_windows_wsl2_programmer.md)
  - [How-to: Debug Nios II and ARM HPS with VS Code](howto_vscode_debugging.md)
