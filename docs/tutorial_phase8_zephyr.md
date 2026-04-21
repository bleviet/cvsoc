# Tutorial — Phase 8: Zephyr RTOS LED Control on DE10-Nano

| | |
|---|---|
| **Series** | CvSoC Tutorials — Phase 8 of 8 |
| **Project** | `12_zephyr_led` |
| **Difficulty** | Advanced |
| **Time** | ~2 hours (excluding downloads) |
| **Prerequisites** | Phases 1–5 complete; DE10-Nano with FPGA bitstream loaded |

## What you will build

A **Real-Time Operating System (RTOS) application** running on the DE10-Nano's
ARM Cortex-A9 HPS under Zephyr RTOS. The application:

- Uses **two Zephyr threads** — one drives LED animations, one handles button input
- Controls the **8 FPGA-fabric LEDs** via the Lightweight HPS-to-FPGA bridge at
  `0xFF200000` using direct memory-mapped I/O (`sys_write32()`)
- Implements **GPIO interrupt callbacks** (`gpio_add_callback()`) for the push
  buttons KEY[0] and KEY[1]
- Is built with **west**, the Zephyr meta-tool, and CMake/Ninja

By the end you will understand the complete Zephyr workflow: workspace
initialisation → Devicetree overlay → Kconfig → application code → build → flash.

---

## Background: Why Zephyr?

Previous phases ran code either as bare-metal (Phases 3–7) or as a Linux
userspace process (Phases 6–7). Zephyr sits in between: it gives you a real
pre-emptive scheduler, semaphores, mutexes, and a hardware abstraction layer,
without needing a full OS kernel. The resulting binary is ~49 KB — versus
many megabytes for a Linux userspace stack.

### Zephyr on Cyclone V SoC

Zephyr runs on the HPS (Hard Processor System) — the ARM Cortex-A9 side of
the Cyclone V SoC. The FPGA fabric remains independent; Zephyr talks to FPGA
peripherals exactly as bare-metal code does: by writing to the Lightweight
HPS-to-FPGA (LW H2F) bridge memory-mapped registers.

| Component | Role |
|---|---|
| ARM Cortex-A9 @ 800 MHz | Runs Zephyr |
| Zephyr scheduler | Pre-emptive thread scheduling |
| LW H2F bridge `0xFF200000` | Write LED PIO DATA register |
| HPS GPIO bank 2 (`gpio1`) | Read KEY[0] / KEY[1] buttons |
| USB-Blaster II | JTAG flash via west/OpenOCD |

---

## Prerequisites

### Hardware

- Terasic DE10-Nano board
- USB-Blaster II cable (built-in, mini-USB)
- USB serial cable (mini-USB J8/UART port)
- FPGA bitstream from Phase 5 or 7 loaded (provides LED PIO at `0xFF200000`)

### Software (install first)

See [`docs/INSTALL_DEPS_PHASE8.md`](INSTALL_DEPS_PHASE8.md) for exact commands.
Summary:

```bash
# [host]
sudo apt-get install -y git cmake ninja-build gperf python3-pip python3-venv

# West meta-tool
pip3 install --user west

# Zephyr SDK 1.0.1 — install to $HOME (not /opt on WSL2!)
cd ~
wget https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v1.0.1/zephyr-sdk-1.0.1_linux-x86_64_gnu.tar.xz
wget -O - https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v1.0.1/sha256.sum \
    | shasum --check --ignore-missing
tar xvf zephyr-sdk-1.0.1_linux-x86_64_gnu.tar.xz
cd ~/zephyr-sdk-1.0.1 && ./setup.sh
```

> **WSL2 Note:** Extract to `$HOME` (the WSL2 ext4 volume `/dev/sdd`) not into
> `/opt` which maps to the Windows C:\ drive. The Windows drive may have limited
> free space.

---

## Step 1 — Project structure overview

```
12_zephyr_led/                   ← project root (in Git)
├── Makefile                     ← make init / build / flash / debug
├── west.yml                     ← reference copy of the west manifest
├── .gitignore                   ← excludes zephyr/, modules/, build/, .west/
├── app/                         ← the Zephyr application (west 'self' project)
│   ├── west.yml                 ← THE manifest west reads (pins Zephyr main)
│   ├── CMakeLists.txt           ← cmake entry: find_package(Zephyr)
│   ├── prj.conf                 ← Kconfig fragment
│   ├── boards/
│   │   └── cyclonev_socdk.overlay  ← DTS overlay for DE10-Nano peripherals
│   └── src/
│       └── main.c               ← application source
└── doc/
    └── README.md                ← project-level readme
```

The west workspace layout after `make init`:

```
12_zephyr_led/           ← west workspace root (.west/ lives here)
├── .west/               ← west internal state (gitignored)
├── app/                 ← our application (manifest repository)
├── zephyr/              ← Zephyr RTOS kernel source (checked out by west)
├── modules/             ← Zephyr module dependencies
└── build/               ← CMake/Ninja build output
```

---

## Step 2 — West manifest and workspace initialisation

The file `app/west.yml` is the **west manifest** — it pins the Zephyr version
and declares any extra modules. For Phase 8 we use Zephyr `main` because it
contains the first release with SDK 1.0.x compatibility:

```yaml
# app/west.yml
manifest:
  remotes:
    - name: zephyrproject-rtos
      url-base: https://github.com/zephyrproject-rtos

  projects:
    - name: zephyr
      remote: zephyrproject-rtos
      revision: main
      import: true    # ← imports all Zephyr module dependencies

  self:
    path: app         # ← west treats app/ as the manifest repo
```

> **Why `import: true`?** Zephyr's own `west.yml` lists dozens of module
> dependencies (hal_intel, lvgl, fatfs, etc.). `import: true` pulls them all
> in automatically without us having to enumerate them.

Initialise the workspace:

```bash
# [host] — from 12_zephyr_led/
make init
# equivalent to:
#   west init -l app/
#   west update
```

This downloads ~1.5 GB. You only need to run it once.

Then install Python dependencies for Zephyr's scripts:

```bash
# [host]
make deps
# equivalent to:
#   pip3 install --user -r zephyr/scripts/requirements.txt
```

---

## Step 3 — Devicetree overlay

Zephyr's hardware model is fully described in Devicetree Source (DTS) files.
The `cyclonev_socdk` board provides a base DTS for the standard Intel SoC kit.
We add a **board overlay** in `app/boards/cyclonev_socdk.overlay` to describe
the DE10-Nano-specific hardware.

### What the overlay adds

**1. FPGA LED PIO node** — documents the Avalon PIO at `0xFF200000`:

```dts
soc {
    fpga_leds: fpga-led-pio@ff200000 {
        compatible = "syscon";
        reg = <0xff200000 0x10>;
        reg-io-width = <4>;
    };
};
```

In `main.c`, we use:
```c
#define LED_PIO_ADDR  DT_REG_ADDR(DT_NODELABEL(fpga_leds))
// DT_REG_ADDR gives us 0xff200000 at compile time
sys_write32(pattern, LED_PIO_ADDR);
```

This is exactly how all previous phases accessed the FPGA LEDs, but now the
address is sourced from the Devicetree at compile time — not hardcoded.

**2. Push-button nodes** — maps KEY[0] and KEY[1] to HPS GPIO:

```dts
buttons {
    compatible = "gpio-keys";
    key0: key_0 {
        gpios = <&gpio1 25 (GPIO_ACTIVE_LOW | GPIO_PULL_UP)>;
    };
    key1: key_1 {
        gpios = <&gpio1 26 (GPIO_ACTIVE_LOW | GPIO_PULL_UP)>;
    };
};

aliases {
    sw0 = &key0;
    sw1 = &key1;
};
```

> **GPIO numbering:** The DE10-Nano KEY[0] is on HPS GPIO pin 54.
> Zephyr's `gpio1` device covers HPS GPIO pins 29–57.
> In the `gpio1` device, pin 54 is bit `54 - 29 = 25`.

---

## Step 4 — Application code

### Thread design

`main.c` creates two Zephyr threads after configuring the GPIO interrupts.

```
                      k_sem_give()
   button_pressed() ───────────────► pattern_sem ──────► led_pattern_thread
   (GPIO ISR — interrupt context)                        (checks sem, changes pattern)
                                     k_sem_take()
   button_monitor_thread ────────────────────────────────► prints log message
```

The pattern semaphore (`K_SEM_DEFINE(pattern_sem, 0, 1)`) decouples the ISR
from both threads. The ISR is deliberately minimal — only `k_sem_give()`.

### LED pattern thread

```c
static void led_pattern_thread(void *p1, void *p2, void *p3)
{
    uint8_t step = 0;
    while (true) {
        // Non-blocking check for pattern change
        if (k_sem_take(&pattern_sem, K_NO_WAIT) == 0) {
            step = 0;
        }
        uint8_t out = compute_frame(current_pattern, step);
        sys_write32((uint32_t)out, LED_PIO_ADDR);  // → FPGA LED PIO
        step++;
        k_msleep(120);   // yields CPU to other threads
    }
}
```

`k_msleep()` is a Zephyr kernel sleep — it yields the CPU to other threads
during the wait. This is fundamentally different from a bare-metal busy-wait.

### GPIO interrupt callback

```c
static void button_pressed(const struct device *dev,
                           struct gpio_callback *cb, uint32_t pins)
{
    current_pattern = (current_pattern + 1) % PATTERN_COUNT;
    k_sem_give(&pattern_sem);
}

// In main():
gpio_pin_configure_dt(&btn0, GPIO_INPUT);
gpio_pin_interrupt_configure_dt(&btn0, GPIO_INT_EDGE_TO_ACTIVE);
gpio_init_callback(&btn0_cb_data, button_pressed, BIT(btn0.pin));
gpio_add_callback(btn0.port, &btn0_cb_data);
```

The `gpio_add_callback()` registers a callback with the Zephyr GPIO subsystem.
When the button fires, the GIC interrupt is captured by Zephyr's ARM interrupt
handler, which dispatches to `button_pressed()` — no manual GIC programming
needed (compare with Phase 7 bare-metal!).

---

## Step 5 — Kconfig

`prj.conf` selects the Zephyr features we need:

```kconfig
CONFIG_GPIO=y              # GPIO subsystem (for button callbacks)
CONFIG_UART_CONSOLE=y      # Route printk/LOG to UART0
CONFIG_PRINTK=y            # Enable printk()
CONFIG_LOG=y               # Enable structured logging (LOG_INF, LOG_ERR)
CONFIG_MULTITHREADING=y    # Thread scheduler (always on for this app)
CONFIG_MAIN_STACK_SIZE=2048
CONFIG_THREAD_NAME=y       # Thread name support (for debug)
```

---

## Step 6 — Build

```bash
# [host] — from 12_zephyr_led/
export ZEPHYR_SDK_INSTALL_DIR=$HOME/zephyr-sdk-1.0.1
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr

make build
```

Or manually with west:

```bash
west build -b cyclonev_socdk --build-dir build app
```

Expected output (last lines):

```
[144/144] Linking C executable zephyr/zephyr.elf
Memory region   Used Size  Region Size  %age Used
           RAM:    1200 KB        1 GB      0.11%
```

Artifacts:

```
build/zephyr/zephyr.elf   ← ELF debug image (used by west flash/debug)
build/zephyr/zephyr.bin   ← raw binary (~49 KB)
```

> **Tip:** Add `WEST_OPTS="--pristine"` to force a clean reconfigure:
> ```bash
> make build WEST_OPTS="--pristine"
> ```

---

## Step 7 — Flash to DE10-Nano

### Prerequisites for flashing

1. **FPGA bitstream loaded:** The LED PIO and button PIO must be in the FPGA.
   Load the Phase 5 or Phase 7 bitstream via Quartus Programmer or:
   ```bash
   quartus_pgm -m jtag -o "p;path/to/soc_system.sof"
   ```

2. **Preloader:** Zephyr's `west flash` loads the SPL (Secondary Program Loader)
   first, then the Zephyr ELF. Download the preloader from the GSRD:
   ```bash
   wget https://releases.rocketboards.org/release/2018.05/gsrd/hw/cv_soc_devkit_ghrd.tar.gz
   tar xvf cv_soc_devkit_ghrd.tar.gz
   cp cv_soc_devkit_ghrd/software/preloader/uboot-socfpga/spl/u-boot-spl \
       zephyr/boards/intel/socfpga_std/cyclonev_socdk/support/
   ```

3. **USB-Blaster II connected** and forwarded to WSL2 via `usbipd`.

### Flash command

```bash
# [host]
make flash
# equivalent to: west flash --build-dir build
```

OpenOCD connects via USB-Blaster II, loads the preloader into OCRAM, then
loads `zephyr.elf` into DDR. Zephyr boots immediately.

---

## Step 8 — Observe and interact

Open a serial terminal at 115200 8N1:

```bash
minicom -D /dev/ttyUSB0 -b 115200 --noinit
```

Expected boot output:

```
*** Booting Zephyr OS build v4.4.99 ***
Phase 8 — Zephyr RTOS LED Demo
Board: cyclonev_socdk
FPGA LED PIO: 0xFF200000
Threads started. Press KEY0 or KEY1 to change pattern.

[00:00:00.001,000] <inf> zephyr_led: LED pattern thread started
[00:00:00.001,000] <inf> zephyr_led: FPGA LED PIO at 0xFF200000
[00:00:00.001,000] <inf> zephyr_led: KEY0 and KEY1 interrupts configured
[00:00:00.001,000] <inf> zephyr_led: Button monitor thread started
```

The 8 FPGA LEDs start animating in the CHASE pattern. Press KEY[0] or KEY[1]
to cycle:

```
CHASE → BREATHE → BLINK → STRIPES → CHASE → …
```

Log output on each button press:

```
[00:00:02.350,000] <inf> zephyr_led: Button pressed — pattern: BREATHE (0x01)
[00:00:05.180,000] <inf> zephyr_led: Button pressed — pattern: BLINK (0xFF)
```

---

## What you have learned

| Concept | Where used |
|---|---|
| **West workspace** | `west init -l app/`, `west update` |
| **West manifest** | `app/west.yml` — pins Zephyr version + imports modules |
| **Devicetree overlay** | `boards/cyclonev_socdk.overlay` — FPGA PIO node, GPIO buttons |
| **DT_NODELABEL / DT_REG_ADDR** | `LED_PIO_ADDR` extracted at compile time from DTS |
| **Zephyr threads** | `k_thread_create()`, `k_thread_name_set()` |
| **Semaphores** | `K_SEM_DEFINE()`, `k_sem_give()`, `k_sem_take()` |
| **GPIO callback** | `gpio_add_callback()`, `GPIO_INT_EDGE_TO_ACTIVE` |
| **MMIO** | `sys_write32()` / `sys_read32()` for FPGA registers |
| **Kconfig** | `prj.conf` — selecting drivers and stack sizes |
| **Scheduler** | `k_msleep()` — yielding CPU between frames |

---

## Troubleshooting

### Build: `Could not find Zephyr-sdk compatible with version 0.16`

Zephyr 3.7.0 / 4.1.0 requires SDK 0.16.x. SDK 1.0.1 deliberately refuses to
satisfy version `0.16` requests (the SDK's CMake version file enforces a `≥ 1.0`
compatibility floor). Use Zephyr `main` which requests SDK `≥ 1.0`.

**Fix:** `app/west.yml` must have `revision: main`.

### Build: `Missing jsonschema dependency`

Zephyr `main` requires a newer Python package set than 3.7.0 LTS.

**Fix:** `make deps` (re-runs `pip3 install -r zephyr/scripts/requirements.txt`
from the newly checked-out `main`).

### Flash: `FATAL ERROR: no USB-Blaster found`

**Fix:** Ensure USB-Blaster II is attached and forwarded to WSL2:
```powershell
# Windows PowerShell (admin)
usbipd list
usbipd bind --busid <ID>
usbipd attach --wsl --busid <ID>
```

### LEDs don't animate

The FPGA bitstream may not have the LED PIO mapped at `0xFF200000`.
Load the Phase 5 or Phase 7 bitstream first, then flash Zephyr.

### Buttons don't change pattern

Verify the DTS overlay is being picked up:
```bash
grep -i "fpga_leds\|key0\|key1" build/zephyr/zephyr.dts
```
If empty, the overlay file name must match the board name exactly:
`app/boards/cyclonev_socdk.overlay`.

---

## References

- [Zephyr Getting Started Guide](https://docs.zephyrproject.org/latest/develop/getting_started/index.html)
- [Zephyr SDK 1.0.1 Releases](https://github.com/zephyrproject-rtos/sdk-ng/releases/tag/v1.0.1)
- [cyclonev_socdk Board Documentation](https://docs.zephyrproject.org/latest/boards/intel/socfpga_std/cyclonev_socdk/doc/index.html)
- [Zephyr Devicetree HowTos](https://docs.zephyrproject.org/latest/build/dts/howtos.html)
- [Cyclone V HPS Technical Reference Manual](https://www.intel.com/content/dam/www/programmable/us/en/pdfs/literature/hb/cyclone-v/cv_54001.pdf)
- [Zephyr GPIO API](https://docs.zephyrproject.org/latest/hardware/peripherals/gpio.html)
- [West documentation](https://docs.zephyrproject.org/latest/develop/west/index.html)
