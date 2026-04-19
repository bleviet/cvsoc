# Phase 6 Tutorial — Embedded Linux on HPS

> **Series:** cvsoc — Stepping into advanced FPGA development on the DE10-Nano  
> **Phase:** 6 of 8  
> **Difficulty:** Intermediate-Advanced — you have completed phases 0–5 and are comfortable with Platform Designer, the Quartus compile flow, ARM bare-metal programming, and hardware interrupt handling

---

## What you will build

By the end of this tutorial you will boot a complete embedded Linux system on the DE10-Nano's ARM Cortex-A9 processor and control the FPGA LEDs from user-space applications:

- **Buildroot** builds a self-contained Linux system: cross-compiler, Linux 6.6 kernel, U-Boot bootloader, and root filesystem — all from a single `make` command
- A **device tree node** binds the LED PIO peripheral to the Linux **UIO (Userspace I/O)** driver framework
- A **C application** (`fpga_led`) maps `/dev/uio0` into user space and writes LED patterns through `mmap()`
- A **Python script** (`fpga_led.py`) demonstrates the same register access using Python's `mmap` module
- A **genimage** configuration produces a ready-to-flash SD card image containing the U-Boot SPL, kernel, device tree, FPGA bitstream, and root filesystem

```
           SD card (sdcard.img)
           ┌─────────────────────────────────────────────────────────────────┐
 Partition │ 1: type 0xA2    │ 2: FAT32 (boot)    │ 3: ext4 (rootfs)       │
           │ U-Boot SPL      │ zImage              │ /usr/bin/fpga_led      │
           │ (preloader)     │ DTB (with UIO node) │ /usr/bin/fpga_led.py   │
           │                 │ u-boot.img          │ /lib/ /etc/ /bin/ ...  │
           │                 │ de10_nano.rbf        │                        │
           │                 │ extlinux.conf        │                        │
           └─────────────────────────────────────────────────────────────────┘

           Boot sequence
           ┌─────────────┐    ┌──────────────┐    ┌────────────────────────┐
           │ Cyclone V    │──►│ U-Boot SPL   │──►│ U-Boot                  │
           │ BootROM      │   │ (partition 1) │   │ loads zImage + DTB     │
           └─────────────┘    └──────────────┘    │ from FAT32 partition   │
                                                   └────────┬───────────────┘
                                                            │
                                                            ▼
           ┌─────────────────────────────────────────────────────────────────┐
           │                     Linux kernel                                │
           │                                                                 │
           │  DTB contains:  base_fpga_region / led-controller@ff200000     │
           │                 compatible = "generic-uio"                      │
           │                          │                                      │
           │                 UIO driver creates /dev/uio0                    │
           │                          │                                      │
           │  User space:    fpga_led ──► mmap(/dev/uio0) ──► LED PIO regs  │
           └─────────────────────────────────────────────────────────────────┘
```

The FPGA design is reused unchanged from project 05 (`05_hps_led`). The only new artefact is the Buildroot-based Linux system.

---

## Prerequisites

| Requirement | Details |
|---|---|
| **Docker** | `raetro/quartus:23.1` image (for SOF → RBF conversion) |
| **Repository** | `git clone` of `bleviet/cvsoc`; phases 0–5 already working |
| **Phase 3** | Project `05_hps_led` built — the SOF file must exist |
| **Host tools** | `wget`, `tar`, `make`, `gcc`, `mtools` (for SD card image generation) |
| **Board** | Terasic DE10-Nano (Cyclone V `5CSEBA6U23I7`) |
| **SD card** | MicroSD card (≥512 MB) and a card reader |
| **Serial console** | USB-UART adapter connected to DE10-Nano UART header (115200 8N1) |

Verify the Docker image and SOF file before continuing:

```bash
docker images | grep raetro/quartus
# Expected: raetro/quartus   23.1   ...

ls 05_hps_led/quartus/de10_nano.sof
# Must exist. Run 'make compile' in 05_hps_led/quartus/ if missing.
```

Install `mtools` if not already present (required by genimage for the FAT32 boot partition):

```bash
sudo apt-get install -y mtools
```

---

## Concepts in 5 minutes

Before touching any file, read these ideas. They explain *why* each component is needed and *how* the pieces fit together.

### From bare-metal to Linux

In phases 3–5 your C code ran directly on the ARM Cortex-A9 with no operating system. The startup assembly set up the exception vector table, initialised the stack, disabled watchdogs, configured the LW H2F bridge, and jumped to `main()`. Your code had direct access to every register.

Linux changes the game:

| Aspect | Bare-metal (Phase 3) | Linux (Phase 6) |
|--------|---------------------|-----------------|
| **Memory access** | Direct physical address (`*(volatile uint32_t *)0xFF200000`) | Via `mmap()` on `/dev/uio0` or `/dev/mem` |
| **Peripheral init** | Manual bridge release, watchdog disable | Kernel + device tree handle it |
| **Build system** | Cross-compiler + linker script | Buildroot (builds entire OS) |
| **Boot** | U-Boot SPL → bare-metal ELF in OCRAM | U-Boot SPL → U-Boot → Linux kernel → init → shell |
| **User interaction** | JTAG debugger | Serial console + SSH |

### Buildroot

[Buildroot](https://buildroot.org/) is a Makefile-based build system that produces a complete embedded Linux image from source. You configure it with a `defconfig` file (similar to the Linux kernel's own Kconfig system), and a single `make` builds:

1. A **cross-toolchain** (GCC + glibc for ARM)
2. The **Linux kernel** (zImage + device tree blob)
3. **U-Boot** bootloader (with SPL for Cyclone V)
4. The **root filesystem** (BusyBox + custom packages)
5. An **SD card image** (partitioned and ready to flash)

Buildroot supports an **external tree** (`BR2_EXTERNAL`) that keeps all board-specific files outside the Buildroot source tree. This project uses an external tree in `br2-external/` to hold the defconfig, board scripts, kernel config fragments, and the `fpga-led` package.

### UIO — Userspace I/O

The Linux UIO framework provides a minimal kernel driver that:

1. Exposes a character device (`/dev/uioN`)
2. Allows user-space code to `mmap()` hardware registers directly
3. Optionally handles interrupts (not used in this project)

This avoids writing a full kernel module. The device tree tells the kernel which physical address range to expose:

```dts
led-controller@ff200000 {
    compatible = "generic-uio";
    reg = <0xff200000 0x10>;
};
```

The UIO driver matches on `compatible = "generic-uio"`, maps the 16-byte register region, and creates `/dev/uio0`. User-space code then opens this device and calls `mmap()` to get a virtual pointer to the LED PIO registers.

### Device tree: how the kernel learns about hardware

The ARM kernel does not probe buses to discover peripherals (unlike x86 PCI). Instead, a **device tree blob (DTB)** describes every device, its address range, compatible driver, and interrupt lines.

For the DE10-Nano, the upstream kernel ships `socfpga_cyclone5_de0_nano_soc.dtb`. This DTB describes the HPS peripherals (UART, Ethernet, GPIO, etc.) but knows nothing about what is in the FPGA fabric. The build system adds a `led-controller` node inside the `base_fpga_region` to describe the LED PIO and bind it to the UIO driver.

### SD card partition layout

The Cyclone V BootROM expects a specific SD card layout:

| Partition | Type | Content | Purpose |
|-----------|------|---------|---------|
| 1 | `0xA2` (custom) | `u-boot-with-spl.sfp` | BootROM loads SPL from here |
| 2 | `0x0C` (FAT32) | zImage, DTB, u-boot.img, RBF, extlinux.conf | U-Boot loads kernel from here |
| 3 | `0x83` (ext4) | Root filesystem | Linux mounts as `/` |

The BootROM scans the MBR partition table for a type-`0xA2` entry and reads the U-Boot SPL (Second Program Loader) from it. The SPL initialises DDR3 SDRAM and loads the full U-Boot from the FAT32 partition. U-Boot then uses its `distro boot` mechanism to find `extlinux/extlinux.conf`, which tells it which kernel and DTB to load.

---

## Project structure

```
10_linux_led/
├── Makefile                          ← Top-level orchestration
├── .gitignore
├── de10_nano.rbf                     ← FPGA bitstream (generated)
├── br2-external/                     ← Buildroot external tree
│   ├── external.desc                 ← BR2_EXTERNAL descriptor
│   ├── external.mk                   ← Package makefile includes
│   ├── Config.in                     ← Package Kconfig menu
│   ├── configs/
│   │   └── de10_nano_defconfig       ← Master Buildroot configuration
│   ├── board/de10_nano/
│   │   ├── genimage.cfg              ← SD card partition layout
│   │   ├── linux-uio.fragment        ← Kernel config: UIO + FPGA Manager
│   │   ├── extlinux.conf             ← U-Boot distro boot config
│   │   ├── post-build.sh             ← Copies Python script to rootfs
│   │   ├── post-image.sh             ← Compiles DT overlay, merges DTB, copies RBF
│   │   └── uboot-env.txt             ← Reference U-Boot environment
│   └── package/fpga-led/
│       ├── Config.in                 ← Buildroot package: fpga-led
│       └── fpga-led.mk              ← generic-package makefile
├── dts/
│   └── fpga_led_overlay.dts          ← Device tree overlay source
├── software/
│   ├── fpga_led/
│   │   ├── fpga_led.c                ← C LED controller (UIO + mmap)
│   │   └── Makefile                  ← Cross-compile Makefile
│   └── fpga_led.py                   ← Python LED controller (UIO or /dev/mem)
└── scripts/
    └── convert_sof_to_rbf.sh         ← SOF → RBF conversion (runs in Docker)
```

---

## Step 1 — Convert the FPGA bitstream

Linux loads FPGA bitstreams in Raw Binary Format (`.rbf`), not the Quartus `.sof` format. Convert the existing bitstream from project 05:

```bash
cd 10_linux_led
make rbf
```

This runs `quartus_cpf` inside the Docker container to produce `de10_nano.rbf` (~7 MB).

> **What happens:** The script `scripts/convert_sof_to_rbf.sh` calls  
> `quartus_cpf -c --option=bitstream_compression=off de10_nano.sof de10_nano.rbf`  
> inside the `raetro/quartus:23.1` container. Compression is disabled because the FPGA Manager expects an uncompressed passive-serial bitstream.

---

## Step 2 — Build the Linux system

### 2.1 Download Buildroot

```bash
make buildroot-download
```

This downloads and extracts Buildroot 2024.11.1 (~8 MB compressed, ~80 MB extracted) into `buildroot-2024.11.1/`.

### 2.2 Configure and build

```bash
make buildroot
```

This single command:

1. Applies the `de10_nano_defconfig` (with `BR2_EXTERNAL` pointing to `br2-external/`)
2. Downloads and builds the ARM cross-compiler (GCC + glibc)
3. Downloads and builds Linux 6.6.86 with the `socfpga` defconfig plus UIO/FPGA kernel config fragment
4. Downloads and builds U-Boot 2024.07 with the `socfpga_de10_nano` defconfig
5. Builds the root filesystem with BusyBox, Python 3, and the `fpga_led` package
6. Runs `post-build.sh` to install `fpga_led.py` into the rootfs
7. Runs `post-image.sh` to compile the device tree overlay, merge the UIO node into the base DTB, and copy the FPGA bitstream
8. Runs `genimage` to assemble the final SD card image

> **First build time:** approximately 15–30 minutes depending on your machine and internet speed. Subsequent builds that only change user-space code take under a minute.

> **WSL2 note:** If your `PATH` contains Windows paths with spaces (common on WSL2), Buildroot will refuse to build. The Makefile handles this, but if you run `make` inside the Buildroot directory directly, sanitise your PATH first:
> ```bash
> export PATH=$(echo "$PATH" | tr ':' '\n' | grep -v ' ' | tr '\n' ':' | sed 's/:$//')
> ```

### 2.3 Verify the output

After a successful build, the images directory contains:

```bash
ls -lh buildroot-2024.11.1/output/images/
```

| File | Size | Description |
|------|------|-------------|
| `sdcard.img` | ~194 MB | Complete SD card image (flash this) |
| `zImage` | ~6 MB | Compressed Linux kernel |
| `socfpga_cyclone5_de0_nano_soc.dtb` | ~20 KB | Device tree blob (with UIO node merged) |
| `u-boot-with-spl.sfp` | ~758 KB | U-Boot SPL + full U-Boot |
| `rootfs.ext4` | 128 MB | Root filesystem |
| `fpga_led_overlay.dtbo` | ~500 B | Compiled device tree overlay |
| `de10_nano.rbf` | ~7 MB | FPGA bitstream (copied from project root) |

---

## Step 3 — Write the SD card

Insert a microSD card into your card reader and identify the device:

```bash
lsblk
# Look for a device matching your SD card size (e.g., /dev/sdb or /dev/mmcblk0)
# DOUBLE CHECK — writing to the wrong device will destroy data!
```

Write the image:

```bash
make flash SDCARD=/dev/sdX
# Or manually:
sudo dd if=buildroot-2024.11.1/output/images/sdcard.img of=/dev/sdX bs=4M status=progress conv=fsync
```

> **Windows/WSL2:** If your SD card reader is on the Windows side, copy the image to a Windows-accessible path and use [balenaEtcher](https://etcher.balena.io/) or Win32DiskImager:
> ```bash
> cp buildroot-2024.11.1/output/images/sdcard.img /mnt/c/Windows/Temp/
> ```
> Then flash `C:\Windows\Temp\sdcard.img` from Windows.

---

## Step 4 — Boot the DE10-Nano

### 4.1 Hardware connections

1. Insert the microSD card into the DE10-Nano's SD card slot
2. Connect a USB-UART adapter to the UART header (J4):
   - Pin 1 (GND) → adapter GND
   - Pin 10 (UART0_TX) → adapter RX
   - Pin 9 (UART0_RX) → adapter TX
3. Open a serial terminal at **115200 baud, 8N1**:
   ```bash
   picocom -b 115200 /dev/ttyUSB0
   # Or: screen /dev/ttyUSB0 115200
   # Or: minicom -D /dev/ttyUSB0 -b 115200
   ```
4. Power on the board (12V barrel connector)

### 4.2 Expected boot output

You should see U-Boot SPL messages, then U-Boot proper, then the Linux kernel:

```
U-Boot SPL 2024.07 (...)
Trying to boot from MMC1

U-Boot 2024.07 (...)
=> ...
Scanning mmc 0:2...
Found /extlinux/extlinux.conf
...
Retrieving file: /zImage
Retrieving file: /socfpga_cyclone5_de0_nano_soc.dtb
...
Starting kernel ...

[    0.000000] Booting Linux on physical CPU 0x0
[    0.000000] Linux version 6.6.86 ...
...
[    1.234567] uio_pdrv_genirq ff200000.led-controller: UIO device registered
...
Welcome to DE10-Nano (cvsoc Phase 6 — Embedded Linux)
de10nano login:
```

Log in as `root` (no password).

> **Key line to look for:** `uio_pdrv_genirq ff200000.led-controller: UIO device registered` — this confirms the device tree node was found and the UIO driver created `/dev/uio0`.

---

## Step 5 — Load the FPGA bitstream

Before controlling the LEDs, the FPGA must be programmed with the LED PIO design. Linux provides the FPGA Manager framework for this:

```bash
# Copy the RBF from the boot partition
mkdir -p /lib/firmware
mount /dev/mmcblk0p2 /mnt
cp /mnt/de10_nano.rbf /lib/firmware/
umount /mnt

# Load the bitstream
echo 0 > /sys/class/fpga_manager/fpga0/flags
echo de10_nano.rbf > /sys/class/fpga_manager/fpga0/firmware
```

If the FPGA Manager sysfs interface is not available (depends on kernel version and config), you can also load the bitstream from U-Boot before booting Linux, or program via JTAG from the host.

---

## Step 6 — Control the LEDs

### 6.1 Verify UIO is working

```bash
ls -la /dev/uio0
# Expected: crw------- 1 root root 243, 0 ... /dev/uio0

cat /sys/class/uio/uio0/name
# Expected: ff200000.led-controller

cat /sys/class/uio/uio0/maps/map0/size
# Expected: 0x00001000 (4096 — one page)
```

### 6.2 Quick test with devmem

BusyBox provides `devmem` for direct register access (useful for quick checks):

```bash
# Read current LED value
devmem 0xFF200000

# Set all LEDs on
devmem 0xFF200000 32 0xFF

# Set alternating pattern
devmem 0xFF200000 32 0xAA
```

### 6.3 Run the C application

```bash
# Cycle through all LED patterns (Ctrl+C to stop)
fpga_led

# Set a specific pattern
fpga_led 0xAA

# Run a named animation
fpga_led --pattern chase
fpga_led --pattern breathe --speed 50

# Show help
fpga_led --help
```

Available patterns:

| Pattern | Description |
|---------|-------------|
| `chase` | Single LED running left to right |
| `breathe` | LEDs fill up then drain |
| `blink` | All 8 LEDs blink on and off |
| `stripes` | Alternating 0xAA / 0x55 pattern |
| `all` | Cycle through all patterns (default) |

### 6.4 Run the Python script

```bash
# Using UIO (default)
fpga_led.py --pattern chase

# Using /dev/mem (requires root, works even without UIO)
fpga_led.py --devmem --value 0x55

# Set speed (in seconds)
fpga_led.py --pattern breathe --speed 0.05
```

---

## Step 7 — Iterate on the application

One of the advantages of Linux is rapid iteration. You can recompile the C application on the host and copy it to the running board over the network:

```bash
# On the host: cross-compile using Buildroot's toolchain
make app-cross ARM_CC=buildroot-2024.11.1/output/host/bin/arm-linux-gnueabihf-gcc

# Copy to the board (if Ethernet is connected)
scp software/fpga_led/fpga_led root@<board-ip>:/usr/bin/

# Or: rebuild the entire image and re-flash
make buildroot
```

For the Python script, simply edit `software/fpga_led.py` on the host and `scp` it to the board — no compilation needed.

---

## Understanding the key files

### Device tree overlay (`dts/fpga_led_overlay.dts`)

```dts
/dts-v1/;
/plugin/;

&base_fpga_region {
    #address-cells = <0x1>;
    #size-cells = <0x1>;

    fpga_led: led-controller@ff200000 {
        compatible = "generic-uio";
        reg = <0xff200000 0x10>;
        status = "okay";
    };
};
```

This overlay targets the `base_fpga_region` node in the Cyclone V device tree. It declares a 16-byte register region at the LW H2F bridge base address (`0xFF200000`) and binds it to the `generic-uio` driver.

> **Build-time merge:** The mainline kernel DTB does not include DT symbols (`__symbols__`), which are required for runtime overlay application. The `post-image.sh` script works around this by decompiling the base DTB, injecting the UIO node via `sed`, and recompiling. The overlay source is kept as a `.dts` file for documentation and potential future use with runtime overlay mechanisms.

### Kernel config fragment (`board/de10_nano/linux-uio.fragment`)

```
CONFIG_UIO=y
CONFIG_UIO_PDRV_GENIRQ=y
CONFIG_FPGA=y
CONFIG_FPGA_MGR_SOCFPGA=y
CONFIG_FPGA_BRIDGE=y
CONFIG_FPGA_REGION=y
CONFIG_OF_FPGA_REGION=y
CONFIG_OF_OVERLAY=y
```

This fragment is applied on top of the `socfpga_defconfig` to enable UIO and FPGA Manager support. Without it, the kernel would not have the `uio_pdrv_genirq` driver and would ignore the `compatible = "generic-uio"` node.

### SD card layout (`board/de10_nano/genimage.cfg`)

The `genimage.cfg` file defines three partitions:

1. **U-Boot SPL** (type `0xA2`, offset 1 MB) — the Cyclone V BootROM searches for this specific partition type
2. **FAT32 boot** (64 MB) — contains zImage, DTB, U-Boot, FPGA bitstream, and `extlinux.conf`
3. **ext4 rootfs** (128 MB) — the full Linux root filesystem

### Buildroot external tree (`br2-external/`)

The external tree keeps all DE10-Nano customisations outside the Buildroot source:

- `external.desc` — declares the external tree name (`DE10_NANO`)
- `configs/de10_nano_defconfig` — the master configuration (architecture, toolchain, kernel, U-Boot, packages)
- `board/de10_nano/` — board-specific scripts and config fragments
- `package/fpga-led/` — Buildroot package definition for the C application

### C application (`software/fpga_led/fpga_led.c`)

The core of the application is straightforward:

```c
int fd = open("/dev/uio0", O_RDWR | O_SYNC);

volatile uint32_t *base = mmap(NULL, 0x1000,
    PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

// Write to the LED PIO DATA register (offset 0x00)
*(base + 0) = 0xAA;  // alternating LEDs on

munmap(base, 0x1000);
close(fd);
```

`mmap()` on a UIO device maps the physical register region into the process's virtual address space. Writes to the mapped memory go directly to the FPGA fabric — no kernel transition, no ioctl, just a store instruction.

---

## Troubleshooting

### Build fails: `You seem to have a path with spaces`

Buildroot cannot handle paths containing spaces. On WSL2, the Windows `PATH` is inherited and often contains spaces. Sanitise it before building:

```bash
export PATH=$(echo "$PATH" | tr ':' '\n' | grep -v ' ' | tr '\n' ':' | sed 's/:$//')
```

### Build fails: `BR2_LEGACY` error

If you see `BR2_LEGACY=y`, your defconfig references a deprecated option. The most common cause is `BR2_PACKAGE_DEVMEM2`, which was removed in Buildroot 2024.11. Use BusyBox's built-in `devmem` instead.

### No `/dev/uio0` after boot

1. Check the kernel log: `dmesg | grep uio`
2. If no UIO messages appear, verify the DTB contains the UIO node:
   ```bash
   mount /dev/mmcblk0p2 /mnt
   dtc -I dtb -O dts /mnt/socfpga_cyclone5_de0_nano_soc.dtb | grep -A3 generic-uio
   umount /mnt
   ```
3. If the node is missing, the `post-image.sh` merge step may have failed — rebuild and check the build output for errors.

### `devmem 0xFF200000` returns `bus error`

The LW H2F bridge is not enabled. This can happen if:
- The FPGA is not programmed (load the RBF first)
- The bridge reset was not released by U-Boot or the kernel

### LEDs don't change after writing to `/dev/uio0`

1. Verify the FPGA is programmed: `cat /sys/class/fpga_manager/fpga0/state` should show `operating`
2. Verify you are writing to the correct offset: the LED PIO DATA register is at offset `0x00` from the mapping base
3. Try `devmem 0xFF200000 32 0xFF` to confirm the hardware path works independently of UIO

### Serial console shows garbage or no output

Verify the baud rate is 115200 and the TX/RX lines are not swapped. The DE10-Nano UART header pinout is:
- Pin 1 (leftmost): GND
- Pin 10: UART0_TX (connect to adapter RX)
- Pin 9: UART0_RX (connect to adapter TX)

---

## What you have learned

| Concept | Where demonstrated |
|---|---|
| Building a complete Linux system with Buildroot | `make buildroot` produces kernel, U-Boot, rootfs, SD card image |
| Buildroot external tree for board customisation | `br2-external/` with defconfig, board scripts, custom package |
| Device tree for FPGA peripherals | `fpga_led_overlay.dts` — UIO binding at `0xFF200000` |
| Kernel config fragments | `linux-uio.fragment` enables UIO + FPGA Manager |
| UIO driver framework | `generic-uio` compatible node → `/dev/uio0` |
| User-space register access via `mmap()` | `fpga_led.c` — `mmap()` on `/dev/uio0` |
| Python mmap for hardware access | `fpga_led.py` — same registers from Python |
| SD card partition layout for Cyclone V | `genimage.cfg` — type 0xA2 SPL + FAT32 + ext4 |
| U-Boot distro boot mechanism | `extlinux.conf` → automatic kernel + DTB loading |
| FPGA bitstream loading from Linux | FPGA Manager sysfs interface |

---

## Next steps

- **Ethernet control (Phase 7):** Add a TCP/UDP server to `fpga_led` so you can control the LEDs remotely from a PC. This builds on the Linux networking stack already present in the image.
- **FPGA Manager automation:** Write a systemd or init.d service that loads the RBF bitstream automatically at boot, before user-space applications start.
- **Custom kernel driver:** Replace the UIO approach with a full `platform_driver` that exposes LED control through the kernel LED subsystem (`/sys/class/leds/`).
- **Runtime device tree overlays:** Rebuild the kernel DTB with symbols (`-@` flag) and apply the overlay at runtime through configfs — useful for hot-plugging FPGA peripherals.
