# Hardware Debug Setup — Programming & Running on the DE10-Nano from WSL2

> **Series:** cvsoc — Stepping into advanced FPGA development on the DE10-Nano  
> **Applies to:** Phases 2 and 3 (04_nios2_led, 05_hps_led)  
> **Difficulty:** Intermediate — you have completed Phase 2 and/or Phase 3 and have pre-built `.sof` and `.elf` files ready

---

## What you will achieve

By the end of this tutorial you will be able to, from a single terminal in WSL2:

- **Program the FPGA** on your DE10-Nano with a pre-built bitstream (`.sof`)
- **Load and run the Nios II firmware** (`04_nios2_led`) via JTAG
- **Load and run the HPS bare-metal application** (`05_hps_led`) into ARM OCRAM via JTAG
- **Understand why** each tool must run where it does, so you can troubleshoot confidently

The build environment for this series uses a Docker container (`raetro/quartus:23.1`) running inside WSL2. This creates a challenge: the USB-Blaster II cable that connects your PC to the DE10-Nano can only be owned by **one consumer at a time** — either the Windows host (for `quartus_pgm.exe`) or the WSL2/Docker environment (for `nios2-download` and `openocd`).

This tutorial walks you through the full workflow, step by step.

---

## Prerequisites

| Requirement | Details |
|---|---|
| **Board** | Terasic DE10-Nano connected via USB (USB-Blaster II built-in) |
| **Built files** | `04_nios2_led/quartus/de10_nano.sof` and/or `05_hps_led/quartus/de10_nano.sof` |
| **Built ELFs** | `04_nios2_led/software/app/nios2_led.elf` and/or `05_hps_led/software/app/hps_led.elf` |
| **Docker** | `raetro/quartus:23.1` image available locally |
| **Windows Quartus Programmer** | `quartus_pgm.exe` accessible at `/mnt/c/intelFPGA_lite/23.1std/qprogrammer/bin64/quartus_pgm.exe` |
| **usbipd-win** | Installed on Windows (see Step 1) |
| **Repository** | `git clone` of `bleviet/cvsoc` with Phases 2 and 3 complete |

Verify your built files are present before continuing:

```bash
ls 04_nios2_led/quartus/de10_nano.sof
ls 04_nios2_led/software/app/nios2_led.elf

ls 05_hps_led/quartus/de10_nano.sof
ls 05_hps_led/software/app/hps_led.elf
ls 05_hps_led/software/app/hps_led.bin   # raw binary needed for OCRAM loading
```

---

## The problem in two minutes

Understanding the constraint saves you debugging time later.

### The toolchain is split across three environments

```
┌──────────────────────────────────────────────────────────────────┐
│  Windows host                                                     │
│  quartus_pgm.exe ─────────────────────────────────┐             │
│  (FPGA bitstream programmer)                       │             │
└────────────────────────────────────────────────────┼─────────────┘
                                                     │
┌────────────────────────────────────────────────────┼─────────────┐
│  WSL2 kernel (shared with Docker containers)        │             │
│                                                     │             │
│  ┌──────────────────────┐   ┌──────────────────┐   │             │
│  │  Docker container    │   │  WSL2 shell      │   │             │
│  │  raetro/quartus:23.1 │   │  usbipd attach/  │   │             │
│  │  nios2-download      │   │  detach          │   │             │
│  │  openocd (jtagd)     │   └──────────────────┘   │             │
│  └──────────────────────┘                          │             │
└────────────────────────────────────────────────────┼─────────────┘
                                                     │ USB-Blaster II
                                              ┌──────▼──────┐
                                              │  DE10-Nano  │
                                              │  JTAG chain │
                                              └─────────────┘
```

### The ownership conflict

The USB-Blaster II is a physical USB device. Windows and WSL2 cannot share it simultaneously:

- When the cable is **not shared** via `usbipd-win`, Windows sees it → `quartus_pgm.exe` works, Docker cannot reach it.
- When the cable is **attached** to WSL2 via `usbipd-win`, Docker with `--privileged` sees it → `nios2-download` and `openocd` work, Windows cannot see it.

The Makefiles handle this automatically. Each target calls `usbipd.exe detach` or `usbipd.exe attach --wsl` as needed before running its tool, so you can call `make program-sof` and `make download-elf` back-to-back without any manual cable management.

### Why nios2-download cannot run bare in Docker

The `nios2-download` shell script shipped with Nios II EDS checks the Linux kernel version string for the word `microsoft`. Docker containers on WSL2 share the host kernel — so the check matches even inside the container. The script then tries to call Windows `.exe` binaries that don't exist inside the container.

The fix is `common/docker/uname_shim.sh`: a tiny `uname` wrapper that strips `microsoft` from the version string. It is bind-mounted over `/usr/local/bin/uname` at container start, taking priority over `/bin/uname`.

### Why Intel's OpenOCD uses aji_client, not usb_blaster

The `openocd` binary bundled with `raetro/quartus:23.1` is Intel's own build. It does not include the generic `usb_blaster` driver. Instead it exposes only `aji_client` — an interface that speaks to a running `jtagd` (Altera JTAG daemon) over a local socket.

The `make download-elf` target for Phase 3 therefore starts `jtagd` first, waits for it to initialise, then runs `openocd`.

---

## Step 1 — Install usbipd-win

`usbipd-win` is a Windows service that lets WSL2 access USB devices. Install it once; it persists across reboots.

Open a **Windows PowerShell (Administrator)** and run:

```powershell
winget install --interactive --exact dorssel.usbipd-win
```

After installation, close and reopen PowerShell. Confirm the service is running:

```powershell
usbipd list
```

You should see your USB-Blaster II in the output. Look for `Altera` in the description or VID `09FB`:

```
BUSID  VID:PID    DEVICE                         STATE
2-4    09fb:6010  USB-Blaster II                 Not shared
```

Note your `BUSID` (e.g. `2-4`). You will use it in every session.

Pre-share the device so WSL2 can attach it without Administrator rights each time:

```powershell
usbipd bind --busid 2-4
```

After binding, `usbipd list` shows `Shared` in the STATE column.

---

## Step 2 — Understand the JTAG chain

To see the devices in the JTAG chain, you can use the `jtagconfig` utility. From WSL2, you can run this command inside the Docker container (ensuring the USB-Blaster is attached via `usbipd` first):

```bash
docker run --rm --privileged -v /dev/bus/usb:/dev/bus/usb raetro/quartus:23.1 jtagconfig
```

**Expected output:**
```
1) DE-SoC [1-1]                               
  4BA00477   SOCVHPS
  02D020DD   5CSEBA6(.|ES)/5CSEMA6/..
```

The output confirms that the DE10-Nano exposes two devices on a single JTAG scan chain via the USB-Blaster II:

| Position | Device | ID Code | Purpose |
|---|---|---|---|
| `@1` | HPS ARM DAP | `0x4BA00477` | Debug access port for the Cortex-A9 |
| `@2` | Cyclone V FPGA | `0x02D020DD` | FPGA configuration and Nios II JTAG |

`quartus_pgm.exe` targets `@2` to program the FPGA bitstream. `nios2-download` and `openocd` both talk to `@1` (HPS DAP) or `@2` (Nios II JTAG) depending on the design.

---

## Step 3 — Program the FPGA

This step uses `quartus_pgm.exe` on the Windows side. The Makefile automatically detaches the USB-Blaster from WSL2 before programming and re-attaches it afterwards.

```bash
# For Phase 2 (Nios II LED)
make program-sof -C 04_nios2_led/quartus

# For Phase 3 (HPS LED)
make program-sof -C 05_hps_led/quartus
```

Expected output:

```
Info (213045): Use File /windows/temp/de10_nano.sof for device 2
Info (209060): Started Programmer operation at ...
Info (209016): Configuring device index 2
Info (209011): Successfully performed operation(s)
Info (209061): Ended Programmer operation at ...
```

> **What the Makefile does:** It copies the `.sof` to `C:\Windows\Temp\` first, then calls `quartus_pgm.exe` with a native Windows path (`C:\Windows\Temp\de10_nano.sof@2`). This is necessary because `quartus_pgm.exe` rejects UNC paths of the form `\\wsl.localhost\Ubuntu\...` that `wslpath -w` would otherwise produce.

The FPGA is now configured. The `CONF_DONE` LED on the DE10-Nano should be lit.

---

## Step 4 — Load and run the Nios II application (04_nios2_led)

This step uses `nios2-download` inside Docker. The USB-Blaster must be **attached to WSL2**.

### 4.1 Download and start the ELF

The Makefile automatically attaches the USB-Blaster to WSL2 before running `nios2-download`.

```bash
make download-elf -C 04_nios2_led/quartus
```

Expected output:

```
Using cable "DE-SoC [1-1]", device 2, instance 0x00
Pausing target processor: OK
Initializing CPU cache (if present)
OK
Downloaded 3KB in 0.2s (15.0KB/s)
Verified OK
Starting processor at address 0x00000020
```

The Nios II CPU is now executing the LED firmware. You should see an 8-bit counter sweeping across `LED[7:0]` on the board.

### 4.2 (Optional) Open the JTAG terminal

The design includes a JTAG UART peripheral. Connect to it to receive any `printf` output from the firmware:

```bash
make terminal -C 04_nios2_led/quartus
# Press Ctrl-C to exit
```

---

## Step 5 — Load and run the HPS bare-metal application (05_hps_led)

This step uses Intel's OpenOCD (inside Docker) with the `aji_client` interface.

> **Note:** The FPGA must already be programmed (Step 3) before loading the HPS application. The HPS firmware drives LEDs through the Lightweight HPS-to-FPGA bridge — the LED PIO peripheral lives in the FPGA fabric, so the bitstream must be present.

### 5.1 Load the binary into OCRAM and start execution

The Makefile automatically ensures the USB-Blaster is attached to WSL2.

```bash
make download-elf -C 05_hps_led/quartus
```

Expected output (abbreviated):

```
Info : Cable 1: hw_name=DE-SoC, port=1-1
Info : JTAG tap: fpgasoc.cpu tap/device found: 0x4ba00477
Info : JTAG tap: fpgasoc.fpga.tap tap/device found: 0x02d020dd
Info : fpgasoc.cpu.0: hardware has 6 breakpoints, 4 watchpoints
target halted in ARM state due to debug-request, current mode: Supervisor
444 bytes written at address 0xffff0000
downloaded 444 bytes in 0.168118s (2.579 KiB/s)
shutdown command invoked
Done: HPS LED application is running.
```

The ARM Cortex-A9 is now executing the LED firmware from OCRAM. You should see the 8-bit LED pattern cycling on the board.

> **Why `.bin` and not `.elf`?**  
> The HPS application is built with `arm-linux-gnueabihf-gcc` (the hard-float Linux toolchain). Even though the code is bare-metal, the Linux toolchain injects ELF program headers that map below OCRAM at `0xfffe0000` — a region that does not exist in a bare-metal context. Loading the ELF directly with OpenOCD causes a data abort.  
>
> The raw binary (`hps_led.bin`), generated alongside the ELF by `objcopy -O binary`, contains only the code and data bytes. OpenOCD's `load_image file.bin 0xFFFF0000 bin` writes them directly to OCRAM without any ELF metadata.

---

## Summary of the full session workflow

```
┌──────────────────────────────────────────────────────────────────┐
│  For each development session:                                    │
│                                                                   │
│  1. Build   →  make compile / make app (inside Docker)           │
│                                                                   │
│  2. Program FPGA + load app (fully automatic):                    │
│     make program-sof    ← detaches cable, programs, re-attaches  │
│     make download-elf   ← attaches cable, loads ELF/BIN, runs    │
└──────────────────────────────────────────────────────────────────┘
```

Quick-reference table:

| Target | Cable managed automatically | What it does |
|---|---|---|
| `program-sof` | detach → Windows → re-attach to WSL2 | Programs FPGA via `quartus_pgm.exe` |
| `download-elf` (Nios II) | attach to WSL2 | Loads ELF via `nios2-download` in Docker |
| `download-elf` (HPS) | attach to WSL2 | Loads binary via OpenOCD in Docker |
| `terminal` (Nios II) | attach to WSL2 | Opens JTAG UART console |

Override the bus ID if yours differs from `2-4`:

```bash
make program-sof USBIPD_BUSID=3-1 -C 04_nios2_led/quartus
```

---

## Troubleshooting

### `quartus_pgm.exe` reports "Cable not detected"

The USB-Blaster is attached to WSL2. Detach it first:

```bash
usbipd.exe detach --busid 2-4
```

Then retry `make program-sof`.

### `nios2-download` reports "No JTAG cables available"

The USB-Blaster is not attached to WSL2. Attach it:

```bash
usbipd.exe attach --wsl --busid 2-4
```

Verify Docker can see the device before retrying:

```bash
lsusb | grep -i altera
```

### `nios2-download` calls `.exe` binaries and fails

The `uname_shim.sh` is not mounted or not executable. Check:

```bash
ls -la common/docker/uname_shim.sh
# Expected: -rwxr-xr-x ...
```

If not executable:

```bash
chmod +x common/docker/uname_shim.sh
```

### OpenOCD reports "data abort" at `0xfffe0000`

You are passing an ELF to OpenOCD's `load_image` instead of the raw binary. The Makefile already uses `hps_led.bin`; this error should not occur unless the `download-elf` target was modified. Confirm:

```bash
grep load_image 05_hps_led/quartus/Makefile
# Expected: load_image /work/05_hps_led/software/app/hps_led.bin 0xFFFF0000 bin
```

If `hps_led.bin` is missing, rebuild the software:

```bash
make app -C 05_hps_led/quartus
ls 05_hps_led/software/app/hps_led.bin
```

### OpenOCD exits immediately with "usb_blaster not found"

The `raetro/quartus:23.1` OpenOCD build supports only `aji_client`. The `de10_nano_hps.cfg` configuration uses `aji_client` by default — if this error appears, the config was edited. Restore it:

```bash
git checkout 05_hps_led/scripts/de10_nano_hps.cfg
```

### `usbipd.exe attach` reports the device is "not shared"

You need to bind the device once in a Windows Administrator shell:

```powershell
usbipd bind --busid 2-4
```

This only needs to be done once per Windows session (or after a reboot resets the binding).

### The FPGA loses its configuration after a power cycle

This is expected. The `.sof` file programs the FPGA's volatile SRAM-based configuration — it is lost when power is removed. The configuration is not written to the flash device on the board. For persistent programming across power cycles you would need to write to the board's flash using `quartus_pgm` in AS (Active Serial) mode, which is outside the scope of this tutorial series.

### HPS LED application loads but LEDs stay off (data abort on `0xFF200000`)

**Symptom:** The binary loads cleanly, execution starts at `0xFFFF0000`, but the LEDs do not light up. If you halt the CPU with OpenOCD, you may see the PC stuck in an exception handler or a data abort in the prefetch/data abort vector.

**Root cause:** The FPGA LED PIO peripheral is held in reset by the `hps_0.h2f_rst_n` signal. In the generated `hps_system` fabric, `h2f_rst_n` feeds an `altera_reset_controller` that drives `led_pio.reset_n`. While this signal is LOW, the Avalon bus interface of the LED PIO does not respond — the AXI bridge returns SLVERR, which the ARM CPU sees as a synchronous external abort (DFSR = `0x8`).

`h2f_rst_n` is asserted (LOW) by the FPGA Manager during JTAG configuration. U-Boot SPL releases the AXI bridges early in boot (`RSTMGR_BRGMODRST = 0x0`), but this does not by itself pulse `h2f_rst_n`. If the application simply clears `BRGMODRST` bits that are already zero, it is a no-op — `h2f_rst_n` was never de-asserted.

A second complication: OpenOCD's MEM-AP issues non-secure AXI transactions. `RSTMGR` lives on the L4 MPU (secure-only) bus, so MEM-AP writes to `RSTMGR_BRGMODRST` are silently discarded. Bridge toggling attempted from an OpenOCD `mww` command has no effect.

**Fix:** The application must explicitly assert and then release all three bridge resets while running in ARM secure supervisor mode:

```c
/* Assert all bridges — drives h2f_rst_n LOW */
RSTMGR_BRGMODRST |= BRGMODRST_ALL;   /* 0x7 */
delay(200000UL);                       /* ~2 ms */

/* Release all bridges — drives h2f_rst_n HIGH */
RSTMGR_BRGMODRST &= ~BRGMODRST_ALL;
delay(200000UL);                       /* let Avalon interconnect settle */
```

This cycle drives `h2f_rst_n` LOW and then HIGH, clocking the 2-stage reset synchroniser in the FPGA fabric and properly de-asserting `led_pio.reset_n`.

**Additional common mistakes for the same symptom:**

| Mistake | Correct value |
|---|---|
| `BRGMODRST_LWHPS2FPGA = (1u << 2)` | `(1u << 1)` (bit 2 is FPGA2HPS) |
| Release only LWHPS2FPGA bridge | Release all three bits simultaneously |
| Write to `SYSMGR_FPGAINTF_EN_2` to enable the bridge | Not needed; that register controls trace/JTAG muxes, not AXI bridges |
