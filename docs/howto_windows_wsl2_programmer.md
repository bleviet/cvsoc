# How-to — Program the DE10-Nano from Windows When Working in WSL2

> **Series:** cvsoc — Stepping into advanced FPGA development on the DE10-Nano  
> **Type:** How-to Guide (problem-oriented)  
> **Audience:** Developers running WSL2 + Docker who prefer to program the FPGA using the Windows-native `quartus_pgm.exe`, or who cannot build the custom `cvsoc/quartus:23.1` Docker image

---

## Overview

The default programming flow in this repository programs the FPGA entirely from WSL2 using `quartus_pgm` inside the `cvsoc/quartus:23.1` Docker container — no Windows installation of Quartus is required.

However, you may prefer the Windows programmer if:

- You have Quartus Prime Lite 23.1 installed on Windows and the Docker image is not yet built
- You want to use the graphical Quartus Programmer UI during bringup or board debugging
- You need to program the flash (AS mode) rather than the volatile SRAM (JTAG mode)
- You are troubleshooting a hardware issue and want to isolate the programmer from Docker

This guide explains how to use `quartus_pgm.exe` on the Windows side while keeping all build and download targets in WSL2/Docker.

---

## Prerequisites

| Requirement | Details |
|---|---|
| **Board** | Terasic DE10-Nano connected via USB (USB-Blaster II) |
| **Built bitstream** | `<phase>/quartus/de10_nano.sof` produced by `make compile` |
| **Windows Quartus** | Quartus Prime Lite 23.1 installed at `C:\intelFPGA_lite\23.1std\` |
| **Quartus Programmer** | `C:\intelFPGA_lite\23.1std\qprogrammer\bin64\quartus_pgm.exe` present |
| **usbipd-win** | Installed on Windows (see below if missing) |
| **Repository** | `git clone` of `bleviet/cvsoc` |

### Install usbipd-win (if not already done)

`usbipd-win` lets WSL2 share USB devices with the Windows host. Install it once in an **Administrator PowerShell**:

```powershell
winget install --interactive --exact dorssel.usbipd-win
```

Then bind the USB-Blaster so WSL2 can attach and detach it without Administrator rights:

```powershell
# Find the BUSID for "Altera USB-Blaster II"
usbipd list

# Bind it (run once, persists across reboots)
usbipd bind --busid <BUSID>
```

Replace `<BUSID>` with the value you found (e.g. `2-4`).

---

## The USB ownership model

The USB-Blaster II is a physical USB device. Windows and WSL2 cannot own it simultaneously:

```
                  usbipd detach (from WSL2)
  WSL2 / Docker ─────────────────────────────► Windows
                                               quartus_pgm.exe works
                                               Docker cannot reach it

                  usbipd attach --wsl (to WSL2)
  Windows ────────────────────────────────────► WSL2 / Docker
                                               nios2-download / openocd work
                                               quartus_pgm.exe cannot reach it
```

Every Makefile in this repository exposes two utility targets for this:

| Target | Effect |
|---|---|
| `make usb-wsl` | Attaches the USB-Blaster to WSL2 (`usbipd attach --wsl --busid <BUSID>`) |
| `make usb-windows` | Detaches the USB-Blaster from WSL2 (`usbipd detach --busid <BUSID>`), handing it back to Windows |

---

## Step 1 — Hand the USB-Blaster to Windows

From your WSL2 terminal, in any phase's `quartus/` directory:

```bash
# Tell usbipd to release the device back to Windows
make usb-windows USBIPD_BUSID=2-4

# Or directly (replace 2-4 with your bus ID):
usbipd.exe detach --busid 2-4
```

Confirm from Windows that the device is no longer attached to WSL2:

```powershell
usbipd list
# Expected: STATE column shows "Shared" (not "Attached")
```

---

## Step 2 — Program with quartus_pgm.exe

The Quartus Programmer uses JTAG device `@2` (the Cyclone V FPGA) in the DE10-Nano chain. The `.sof` file cannot be referenced via a UNC path (`\\wsl.localhost\...`), so copy it to a native Windows temporary location first.

```bash
# From the phase's quartus/ directory in WSL2:
REVISION=de10_nano
cp ${REVISION}.sof /mnt/c/Windows/Temp/${REVISION}.sof

# Invoke quartus_pgm.exe via Windows interop (no wine, no extra setup):
/mnt/c/intelFPGA_lite/23.1std/qprogrammer/bin64/quartus_pgm.exe \
  -m jtag -o "p;C:\Windows\Temp\\${REVISION}.sof@2"
```

Expected output:

```
Info (213045): Use File C:/Windows/Temp/de10_nano.sof for device 2
Info (209060): Started Programmer operation at ...
Info (209016): Configuring device index 2
Info (209011): Successfully performed operation(s)
Info (209061): Ended Programmer operation at ...
```

The `CONF_DONE` LED on the DE10-Nano should illuminate, confirming the FPGA is programmed.

> **Override the programmer path** if your Quartus is installed elsewhere:
> ```bash
> QUARTUS_PGM=/mnt/c/path/to/quartus_pgm.exe
> ${QUARTUS_PGM} -m jtag -o "p;C:\Windows\Temp\de10_nano.sof@2"
> ```

---

## Step 3 — Return the USB-Blaster to WSL2

After programming, attach the USB-Blaster back to WSL2 so that `nios2-download`, `openocd`, and `jtagd` can reach it:

```bash
make usb-wsl USBIPD_BUSID=2-4

# Or directly:
usbipd.exe attach --wsl --busid 2-4
```

Verify Docker can see the device:

```bash
docker run --rm --privileged -v /dev/bus/usb:/dev/bus/usb cvsoc/quartus:23.1 \
  bash -c 'jtagd && sleep 2 && jtagconfig'
```

Expected:

```
1) DE-SoC [1-1]
  4BA00477   SOCVHPS
  02D020DD   5CSEBA6(.|ES)/5CSEMA6/..
```

You can now run `make download-elf` as normal.

---

## Combining steps into a single session

A typical session that uses the Windows programmer for `program-sof` then switches back to WSL2 for `download-elf`:

```bash
# 1. Build (all inside Docker, no USB access needed)
docker run --rm -v $(pwd):/work cvsoc/quartus:23.1 \
  bash -c "cd /work/05_hps_led/quartus && make all"

# 2. Hand USB to Windows, program the FPGA
cd 05_hps_led/quartus
make usb-windows USBIPD_BUSID=2-4
cp de10_nano.sof /mnt/c/Windows/Temp/de10_nano.sof
/mnt/c/intelFPGA_lite/23.1std/qprogrammer/bin64/quartus_pgm.exe \
  -m jtag -o "p;C:\Windows\Temp\de10_nano.sof@2"

# 3. Return USB to WSL2, load the HPS application
make usb-wsl USBIPD_BUSID=2-4
make download-elf USBIPD_BUSID=2-4
```

> **Tip:** If you run all three phases frequently, set `USBIPD_BUSID` once in your shell:
> ```bash
> export USBIPD_BUSID=2-4
> ```
> All `make` targets in the repository read it automatically.

---

## Using the Graphical Quartus Programmer

If you prefer the point-and-click GUI:

1. Run `make usb-windows` (or `usbipd.exe detach`) from WSL2 to release the USB-Blaster.
2. Open **Quartus Prime** on Windows → **Tools** → **Programmer**.
3. Click **Hardware Setup** and select `USB-Blaster II`.
4. Click **Auto Detect** — you should see the two-device JTAG chain.
5. Double-click the row for device `@2` (5CSEBA6), select **Program/Configure**, and browse to `C:\intelFPGA_lite\...` or copy the `.sof` there first.
6. Click **Start**.
7. After programming completes, close the Programmer and run `make usb-wsl` from WSL2 to reclaim the USB-Blaster.

---

## Scripting a complete Windows-programmer session

For projects where you always use the Windows programmer, you can add a custom `program-sof-win` target to the Makefile by overriding the phase Makefile locally:

```makefile
# Paste into <phase>/quartus/GNUmakefile (overrides Makefile for this directory)
QUARTUS_PGM_WIN ?= /mnt/c/intelFPGA_lite/23.1std/qprogrammer/bin64/quartus_pgm.exe
SOF_WIN_TMP     := /mnt/c/Windows/Temp

include Makefile

program-sof-win: usb-windows $(REVISION_NAME).sof
	cp $(REVISION_NAME).sof $(SOF_WIN_TMP)/$(REVISION_NAME).sof
	$(QUARTUS_PGM_WIN) -m jtag \
	  -o "p;C:\Windows\Temp\$(REVISION_NAME).sof@$(DEVICE_INDEX)"
	@$(MAKE) --no-print-directory usb-wsl
```

```bash
make program-sof-win -C 05_hps_led/quartus
```

This keeps the repository's Makefile unchanged while giving you a per-project override.

---

## Troubleshooting

### `quartus_pgm.exe` reports "Cable not detected"

The USB-Blaster is still attached to WSL2. Detach it:

```bash
make usb-windows USBIPD_BUSID=2-4
```

Then retry `quartus_pgm.exe`.

### `quartus_pgm.exe` reports "Unsupported programming file path"

`quartus_pgm.exe` rejects UNC paths (`\\wsl.localhost\...`). Always copy the `.sof` to a native Windows path first:

```bash
cp de10_nano.sof /mnt/c/Windows/Temp/de10_nano.sof
```

Then use `C:\Windows\Temp\de10_nano.sof` in the `-o` argument.

### `usbipd.exe detach` says "Not attached"

The USB-Blaster is already with Windows (or not bound). Nothing to do — proceed directly to `quartus_pgm.exe`.

### `usbipd.exe attach --wsl` says "Device is not shared"

Bind the device once in an Administrator PowerShell:

```powershell
usbipd bind --busid 2-4
```

This step only needs to be done once per machine, not once per session.

### The JTAG chain shows only one device or `0x00000000`

Power-cycle the DE10-Nano. The FPGA needs `VCCIO` stable before the JTAG chain is valid.

---

## Relationship to the standard WSL2 flow

The standard `make program-sof` target (the default in all Makefiles) runs `quartus_pgm` **inside Docker**, keeping the entire workflow in WSL2. The Windows flow documented here is a fallback. Both flows:

- Use the same `.sof` output from `make compile`
- Leave `download-elf` and all GDB targets unchanged (they always run in Docker)
- Require the USB-Blaster to be attached to WSL2 (`make usb-wsl`) before `download-elf`

To switch back to the standard flow from any project directory:

```bash
make program-sof USBIPD_BUSID=2-4
```
