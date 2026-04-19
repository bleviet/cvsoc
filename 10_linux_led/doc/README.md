# 10 — Embedded Linux on HPS

Boot a custom Linux system on the DE10-Nano's ARM Cortex-A9, program the
FPGA fabric at boot-time using a custom kernel module with direct FPGA Manager
register access, and drive the on-board LEDs from a user-space C application
via `/dev/mem` mmap.

This is the first project in the series where the ARM processor runs a real
operating system. Everything from the bootloader to the LED animation is built
from source by **Buildroot** in a single `make` command.

---

## What you will build

```
 ┌─────────────────────────────────────────────────────────────────────┐
 │                    Cyclone V SoC                                     │
 │                                                                      │
 │  ┌──────────────────────┐   LW H2F Bridge   ┌───────────────────┐   │
 │  │  ARM Cortex-A9 (HPS) │ ◄────────────────► │  LED PIO (FPGA)  │   │
 │  │                      │   0xFF200000        │  8-bit output    │   │
 │  │  Linux 6.6           │                    └──────┬────────────┘   │
 │  │  │                   │                           │                │
 │  │  ├─ fpga_load.ko  ──────► FPGA Mgr regs ────► FPGA CONFIG       │
 │  │  │  (programs FPGA)  │   (direct access)                         │
 │  │  │                   │                                            │
 │  │  └─ /dev/mem mmap ───────────────────────────► LED PIO regs    │
 │  │     (0xFF200000)     │                                            │
 │  │                      │                                            │
 │  │  fpga_led (app) ──────────────────────────────► LED patterns    │
 │  └──────────────────────┘                                            │
 │                                                                      │
 │  SD card boot: BootROM → U-Boot SPL → U-Boot → Linux               │
 └─────────────────────────────────────────────────────────────────────┘
                                        │
                               LED[7:0] │
                                   ┌────▼────┐
                                   │ DE10-Nano│
                                   │  board   │
                                   └──────────┘
```

**After completing this tutorial you will be able to:**

- Explain the Cyclone V SoC boot flow from BootROM to Linux userspace
- Build a complete embedded Linux system with Buildroot
- Program the FPGA from a running Linux system using a kernel module
- Access FPGA peripherals from user space via `/dev/mem` mmap
- Control hardware with a C application that requires no kernel driver changes

---

## Prerequisites

**Hardware:**
- DE10-Nano development board
- microSD card (4 GB minimum, class 10 recommended)
- USB-to-UART adapter (3.3 V, FTDI FT232 or compatible)
- USB power supply (5 V, 2 A)

**Software (host machine, Linux or WSL2):**
- Buildroot dependencies:
  ```bash
  sudo apt install build-essential libncurses-dev bc git rsync wget unzip \
      cpio file mtools dosfstools
  ```
- `dtc` (Device Tree Compiler): included as a Buildroot host tool, no separate install needed
- A terminal emulator for UART (e.g., `minicom`, `screen`, `picocom`)

**Knowledge:**
- Completed Phases 1–5 of the `cvsoc` series (or equivalent experience)
- Basic familiarity with Linux shell, C programming, and cross-compilation concepts

> **FPGA design:** This project reuses the FPGA design from `05_hps_led`. The
> `.rbf` bitstream file (`de10_nano.rbf`) in the `10_linux_led/` directory was
> generated from that design.

---

## Architecture overview

Before building anything, it helps to understand the three layers of the system
and how they fit together at runtime.

### Boot flow

```
1. BootROM (hardwired in HPS silicon)
       Reads U-Boot SPL from SD card partition type 0xA2

2. U-Boot SPL (Secondary Program Loader)
       Initialises DDR3, clocks, and PLL
       Loads U-Boot proper from the FAT boot partition

3. U-Boot
       Searches FAT partition for boot.scr (auto-boot script)
       Loads zImage + DTB into DDR3
       Passes control to the Linux kernel

4. Linux kernel (6.6.86)
       Probes hardware via Device Tree
       Starts the FPGA Manager driver (socfpga-fpga-mgr)
       Starts the FPGA Region driver — registers the base_fpga_region

5. Init system (BusyBox)
       Runs /etc/init.d/S30fpga_load
       → insmod fpga_load.ko: programs FPGA from /lib/firmware/de10_nano.rbf
       → devmem: enables the LW H2F bridge
       /dev/uio0 appears (LED PIO is now accessible)

6. User space
       fpga_led opens /dev/uio0, mmap()s the LED PIO registers, animates LEDs
```

### Device Tree role

The Device Tree (`.dtb`) describes the hardware to the Linux kernel. For this
project the key nodes are:

```
/soc/fpgamgr@ff706000          — FPGA Manager (programs bitstream)
/soc/base_fpga_region          — FPGA Region (holds firmware-name, bridge ref)
    firmware-name = "de10_nano.rbf"   — bitstream in /lib/firmware/
    fpga-bridges = <&lw_h2f_bridge>   — must be enabled before UIO works
    led-controller@ff200000    — UIO device (generic-uio driver)
/soc/fpga_bridge@ff400000      — LW H2F bridge (status = "okay")
```

The `post-image.sh` script patches these properties into the stock Buildroot
DTB using `fdtput` at image-build time. No manual DTS editing is required.

### UIO framework

The Linux UIO framework (`CONFIG_UIO_PDRV_GENIRQ`) allows a user-space program
to `mmap()` memory-mapped I/O regions without writing a kernel driver. A device
node (`/dev/uio0`) is created automatically when the kernel sees a DT node with
`compatible = "generic-uio"`. The `fpga_led` application then maps the LED PIO
register file directly into its own address space.

---

## Step 1 — Clone Buildroot

The `10_linux_led/` directory contains a Buildroot *external tree*
(`br2-external/`) but not Buildroot itself. The exact Buildroot version (2024.11.1)
is pinned in the top-level `Makefile`.

```bash
cd 10_linux_led/
make buildroot-download   # downloads buildroot-2024.11.1.tar.gz and extracts it
```

If `make buildroot-download` is unavailable, do it manually:

```bash
wget https://buildroot.org/downloads/buildroot-2024.11.1.tar.gz
tar xf buildroot-2024.11.1.tar.gz
```

---

## Step 2 — Configure and build

### 2.1 Apply the defconfig

```bash
cd 10_linux_led/buildroot-2024.11.1
make BR2_EXTERNAL=../br2-external de10_nano_defconfig
```

This command copies the project's `defconfig` into Buildroot's `.config`. The
defconfig specifies, among other things:

| Option | Value |
|--------|-------|
| Architecture | ARM Cortex-A9 (EABI, hard-float NEON) |
| Toolchain | Buildroot internal glibc |
| Kernel | Linux 6.6.86, `socfpga_defconfig` |
| U-Boot | 2024.07, `socfpga_de10_nano` defconfig |
| Root filesystem | ext4, 128 MB |
| Extra packages | `fpga-led` (C app), `fpga-mgr-load` (kernel module), Python 3 |

### 2.2 Build everything

```bash
make
```

The first build downloads ~1.2 GB of source archives and takes 45–90 minutes
on a modern workstation. Subsequent incremental builds are seconds.

**What gets built:**

| Artifact | Path | Description |
|----------|------|-------------|
| `u-boot-with-spl.sfp` | `output/images/` | U-Boot + SPL combined image |
| `zImage` | `output/images/` | Compressed Linux kernel |
| `socfpga_cyclone5_de0_nano_soc.dtb` | `output/images/` | Patched Device Tree |
| `rootfs.ext4` | `output/images/` | Root filesystem |
| `boot.scr` | `output/images/` | U-Boot auto-boot script |
| `sdcard.img` | `output/images/` | Final SD card image |

> The `post-image.sh` script runs automatically at the end of `make`. It uses
> `fdtput` to patch the DTB and `mkimage` to create `boot.scr` — you do not
> need to run these tools manually.

---

## Step 3 — Flash the SD card

> This is the one step that requires a human to insert physical media.

### On Linux / WSL2

```bash
# Identify your SD card device (replace sdX with your actual device)
lsblk

# Flash the image
sudo dd if=output/images/sdcard.img of=/dev/sdX bs=4M status=progress conv=fsync
sudo sync
```

**Warning:** Double-check the target device (`/dev/sdX`). Writing to the wrong
device will overwrite data irrecoverably.

### On Windows

Use [Balena Etcher](https://etcher.balena.io/) or `Win32DiskImager`. Select
`output/images/sdcard.img` as the source and your SD card as the target.

---

## Step 4 — First boot

### 4.1 Connect the UART

Connect the USB-to-UART adapter between your host PC and the DE10-Nano's
UART header (**J4**, the 10-pin header near the power barrel connector):

| DE10-Nano J4 pin | Signal    | Adapter pin |
|-----------------|-----------|-------------|
| Pin 1           | GND       | GND         |
| Pin 10          | UART0_TX  | RX          |
| Pin 9           | UART0_RX  | TX          |

Open a terminal at **115200 8N1**:

```bash
# Linux / WSL2
picocom -b 115200 /dev/ttyUSB0

# Or with screen:
screen /dev/ttyUSB0 115200
```

### 4.2 Power the board

Insert the flashed SD card and connect USB power. You should see the U-Boot
banner within ~2 seconds:

```
U-Boot SPL 2024.07 (...)
...
U-Boot 2024.07 (...)
...
## Executing script at 02000000
=== cvsoc Phase 6 — Embedded Linux boot ===
...
Starting kernel ...

Welcome to DE10-Nano (cvsoc Phase 6 — Embedded Linux)
de10nano login: root
#
```

> **No U-Boot prompt interaction is needed.** `boot.scr` contains the boot
> commands and is found automatically by U-Boot's `distro_bootcmd`.

Log in as `root` with no password.

### 4.3 Verify the boot

```bash
# Check the kernel version
uname -r
# → 6.6.86

# Check FPGA Manager state
cat /sys/class/fpga_manager/fpga0/state
# → operating   (FPGA is programmed)

# Check that /dev/uio0 appeared
ls -la /dev/uio*
# → crw-rw-rw-  1 root root 247, 0  /dev/uio0
```

If `/dev/uio0` is present and the FPGA state is `operating`, you are ready.

---

## Step 5 — FPGA programming (what happens at boot)

At boot, BusyBox init runs `/etc/init.d/S30fpga_load`. This script:

1. **Loads `fpga_load.ko`** — a tiny kernel module that calls the kernel's
   `fpga_mgr_load()` function directly:
   ```bash
   insmod /lib/modules/$(uname -r)/fpga_load.ko firmware=de10_nano.rbf
   ```
   The kernel's FPGA Manager (`socfpga-fpga-mgr`) takes over: it resets the
   FPGA, streams the RBF bitstream from `/lib/firmware/de10_nano.rbf` over the
   FPGA configuration data bus, and waits for CONF_DONE to assert.

2. **Enables the LW H2F bridge** — once the FPGA is in USER_MODE, the bridge
   must be taken out of reset before peripherals inside the FPGA are accessible:
   ```bash
   # Clear bit 1 of BRGMODRST (Bridge Module Reset register)
   devmem 0xFFD0501C 32 $(printf '0x%08X' $(($(devmem 0xFFD0501C 32) & ~2)))
   ```

3. **`/dev/uio0` appears** — the kernel's UIO platform driver probes the
   `led-controller@ff200000` DT node (now that the region is fully operational)
   and creates the character device.

You can watch this happen in the kernel log:

```bash
dmesg | grep -E "fpga|uio"
# [    1.078440] of-fpga-region soc:base_fpga_region: FPGA Region probed
# [    4.312100] fpga_load: loading firmware 'de10_nano.rbf'
# [    4.318200] fpga_load: firmware size = 7007204 bytes
# [    4.921300] fpga_load: FPGA programmed successfully!
# [    4.935100] uio_pdrv_genirq: base_fpga_region:led-controller@ff200000: registered UIO device uio0
```

---

## Step 6 — Control the LEDs

### 6.1 Run the LED application

```bash
fpga_led
```

This starts the default animation: a sequence of patterns cycling through
chase, breathe, blink, and stripe effects. Press **Ctrl+C** to stop.

```bash
# Set a fixed pattern (all LEDs on)
fpga_led 0xFF

# Run a single named animation
fpga_led --pattern chase
fpga_led --pattern breathe
fpga_led --pattern blink
fpga_led --pattern stripes

# Change animation speed (milliseconds per step)
fpga_led --pattern chase --speed 50

# Show help
fpga_led --help
```

The LEDs (LED[7:0]) are the red LEDs on the left side of the DE10-Nano board.

### 6.2 Control via Python (direct register access)

The `fpga_led.py` script demonstrates an alternative approach using Python
`mmap` to access the LED PIO register directly — no UIO device needed:

```bash
python3 /usr/bin/fpga_led.py 0xAA
```

This maps `/dev/mem` at physical address `0xFF200000` (the LED PIO DATA
register) and writes the pattern directly. It is a useful debugging technique
but bypasses the kernel's device model entirely.

---

## How the code works

### `fpga_load.c` — the FPGA programmer module

The `fpga_load.ko` module is loaded once at boot and then can be removed. Its
`init` function performs these steps:

```c
// 1. Find the FPGA Manager in the Device Tree
node = of_find_compatible_node(NULL, NULL, "altr,socfpga-fpga-mgr");
mgr  = of_fpga_mgr_get(node);

// 2. Request the .rbf file from /lib/firmware/
request_firmware(&fw, "de10_nano.rbf", &mgr->dev);

// 3. Fill in the image descriptor
info->buf   = fw->data;
info->count = fw->size;
info->flags = 0;           // full reconfiguration (not partial)

// 4. Lock the manager and load
fpga_mgr_lock(mgr);
fpga_mgr_load(mgr, info);  // internally: RESET → CONFIG → USER_MODE
fpga_mgr_unlock(mgr);
```

The kernel's `fpga_mgr_load()` handles all the low-level sequencing: it reads
the MSEL pins from the STAT register to determine the correct `CDRATIO` and
`CFGWDTH` settings, manages the NCONFIGPULL/NSTATUS handshake, streams the data
through the configuration data port, and waits for CONF_DONE.

### `fpga_led.c` — the LED controller

The user-space application opens `/dev/uio0` and maps the LED PIO register file:

```c
// Open the UIO character device
int fd = open("/dev/uio0", O_RDWR | O_SYNC);

// Map the hardware registers into the process's virtual address space
volatile uint32_t *base = mmap(NULL, 0x1000,
    PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

// Write directly to the LED PIO DATA register (offset 0x00)
*base = 0xAA;   // sets LEDs to alternating pattern 1010 1010
```

The UIO kernel subsystem translates the `mmap()` offset into the physical
address registered in the Device Tree (`0xFF200000`). No kernel driver is
involved in the subsequent register reads and writes — they go directly from the
process's virtual address to the ARM AXI bus and through the LW H2F bridge to
the Avalon PIO peripheral inside the FPGA.

### `post-image.sh` — the DTB patcher

The Cyclone V Device Tree supplied with mainline Linux does not include the
`firmware-name`, `fpga-bridges`, or `ranges` properties that `of-fpga-region`
requires, and the LW H2F bridge node is disabled by default. The `post-image.sh`
Buildroot hook patches these at image-build time using `fdtput`:

```bash
# Give the LW H2F bridge a phandle so base_fpga_region can reference it
fdtput -t x $DTB /soc/fpga_bridge@ff400000 phandle 0x40
fdtput -t s $DTB /soc/fpga_bridge@ff400000 status okay

# Tell the FPGA Region where to find the bitstream and which bridge to use
fdtput    $DTB /soc/base_fpga_region ranges
fdtput -t s $DTB /soc/base_fpga_region firmware-name de10_nano.rbf
fdtput -t x $DTB /soc/base_fpga_region fpga-bridges 0x40
```

Adding the `led-controller@ff200000` UIO child node requires a full DTS
round-trip because `fdtput` (DTC 1.7.x) can modify existing nodes but cannot
create new ones. `post-image.sh` decompiles the DTB to DTS, injects the node
with a Python one-liner, then recompiles:

```bash
dtc -I dtb -O dts -o patched.dts $DTB
python3 -c "..." patched.dts    # inject led-controller@ff200000 block
dtc -I dts -O dtb -o $DTB patched.dts
```

This approach is reliable (no sed-based text surgery on DTS files), the changes
are expressed in code, and the output can be verified with `fdtget`.

---

## Directory structure

```
10_linux_led/
├── br2-external/                         # Buildroot external tree
│   ├── external.desc
│   ├── Config.in                         # Package menu entries
│   ├── external.mk                       # Auto-includes all package .mk files
│   ├── configs/
│   │   └── de10_nano_defconfig           # Buildroot configuration
│   └── board/
│       └── de10_nano/
│           ├── extlinux.conf             # U-Boot sysboot fallback config
│           ├── genimage.cfg              # SD card partition layout
│           ├── linux-uio.fragment        # Kernel config additions (UIO, FPGA, modules)
│           ├── post-build.sh             # Copies RBF, init script into rootfs
│           ├── post-image.sh             # Patches DTB, generates boot.scr
│           └── S30fpga_load              # Init script: program FPGA at boot
│   └── package/
│       ├── fpga-led/                     # User-space LED app Buildroot package
│       └── fpga-mgr-load/                # fpga_load.ko Buildroot package
├── dts/
│   └── fpga_led_overlay.dts              # DT overlay source (reference)
├── software/
│   ├── fpga_led/
│   │   ├── Makefile
│   │   └── fpga_led.c                    # C app: animates LEDs via /dev/uio0
│   ├── fpga_load/
│   │   ├── Kbuild
│   │   └── fpga_load.c                   # Kernel module: programs FPGA at boot
│   └── fpga_led.py                       # Python alternative using /dev/mem mmap
├── scripts/
│   └── convert_sof_to_rbf.sh             # Convert .sof → .rbf for FPGA Manager
├── doc/
│   └── README.md                         ← this file
├── de10_nano.rbf                         # FPGA bitstream (from 05_hps_led)
└── Makefile                              # Top-level: buildroot-download, build, image
```

---

## Memory map

| Region | Physical address | Size | Description |
|--------|-----------------|------|-------------|
| OCRAM  | `0xFFFF0000` | 64 KB | On-chip RAM (used by SPL) |
| LW H2F bridge base | `0xFF200000` | 2 MB | FPGA peripherals via AXI-to-Avalon bridge |
| LED PIO DATA | `0xFF200000` | 4 B | Avalon PIO write register (8 LEDs) |
| LED PIO DIRECTION | `0xFF200004` | 4 B | I/O direction (all outputs) |
| FPGA Manager CSR | `0xFF706000` | 4 KB | FPGA config state machine |
| FPGA Manager data | `0xFFB90000` | 4 B | Configuration data port (write-only) |
| Bridge module reset | `0xFFD0501C` | 4 B | BRGMODRST: bit 1 = LW H2F reset |

---

## Building the RBF bitstream

If you want to regenerate `de10_nano.rbf` from the Quartus project in
`05_hps_led/`, run:

```bash
cd 05_hps_led/quartus
make all                      # synthesise and compile the FPGA design

# Convert the .sof to the flat binary format (.rbf) required by FPGA Manager
quartus_cpf -c \
    --option=bitstream_compression=on \
    output_files/de10_nano.sof \
    ../10_linux_led/de10_nano.rbf
```

The `--option=bitstream_compression=on` flag is **required** for the DE10-Nano.
The board's MSEL switches are set to `0x0A` (FPPx32 with Decompression), which
means the FPGA's hardware decompression engine is active and expects compressed
data. Using an uncompressed RBF will cause CONF_DONE to never assert.

---

## Troubleshooting

### U-Boot stops at `=>` prompt (no auto-boot)

The `boot.scr` file is missing from the FAT partition. This can happen if
`mkimage` was not available during the build. Check the `post-image.sh` output
for the line `boot.scr created for U-Boot auto-boot`.

**Manual workaround** at the U-Boot prompt:

```
=> sysboot mmc 0:2 any ${scriptaddr} /extlinux.conf
```

### `/dev/uio0` does not appear

Check each layer in order:

```bash
# 1. Is the FPGA programmed?
cat /sys/class/fpga_manager/fpga0/state
# Expected: operating

# 2. Did the FPGA Region probe correctly?
dmesg | grep "fpga-region"
# Expected: of-fpga-region soc:base_fpga_region: FPGA Region probed

# 3. Did the UIO device register?
dmesg | grep uio
# Expected: uio_pdrv_genirq: ... registered UIO device uio0

# 4. Is the LW H2F bridge enabled?
devmem 0xFFD0501C 32
# Bit 1 should be 0 (bridge out of reset)
```

If the FPGA state is not `operating`:
```bash
# Manually re-run the FPGA programmer
insmod /lib/modules/$(uname -r)/fpga_load.ko firmware=de10_nano.rbf
dmesg | grep fpga_load
```

### `fpga_led` prints "cannot open /dev/uio0: No such file or directory"

The UIO module may not be loaded:

```bash
modprobe uio_pdrv_genirq
ls /dev/uio*
```

If the module is loaded but the device still does not appear, the LED PIO DT
node is not being probed. Verify the DTB has the correct properties:

```bash
cat /proc/device-tree/soc/base_fpga_region/led-controller@ff200000/compatible
# Expected: generic-uio
```

### LEDs do not respond to writes

The LW H2F bridge may not be enabled. Check and enable it:

```bash
# Read BRGMODRST — bit 1 must be 0 for LW H2F to be active
devmem 0xFFD0501C 32
# If bit 1 is set (value has 0x2 OR'd in), clear it:
devmem 0xFFD0501C 32 0x00000000
```

### Build fails: "Your PATH contains spaces"

This is a WSL2 issue caused by Windows paths leaking into `$PATH`. Build with:

```bash
PATH=$(echo $PATH | tr ':' '\n' | grep -v ' ' | tr '\n' ':') make
```

---

## Key concepts learned

| Concept | Where it appears |
|---------|-----------------|
| SoC boot flow (BootROM → SPL → U-Boot → Linux) | `genimage.cfg` partitioning, U-Boot `boot.scr` |
| Buildroot external tree | `br2-external/` directory structure |
| Device Tree bindings | `post-image.sh` DTB patching, `fdtput` |
| Linux FPGA Manager API | `fpga_load.c`, `S30fpga_load` |
| UIO (Userspace I/O) framework | `fpga_led.c`, DT `compatible = "generic-uio"` |
| Memory-mapped I/O from userspace | `mmap()` call in `fpga_led.c` |
| BusyBox init.d scripts | `S30fpga_load` naming convention (S=start, 30=order) |
| `fdtput` / `fdtget` — scriptable DTB editing | `post-image.sh` |
