# Command Reference — cvsoc Tutorial Series

> **Type:** Reference  
> **Series:** cvsoc — Stepping into advanced FPGA development on the DE10-Nano  
> **Applies to:** All phases (00 – 11)

---

## How to read this reference

Commands in this project are invoked through **`make` targets** inside each phase's `quartus/` directory, always running inside the `cvsoc/quartus:23.1` Docker container. The sections below document every command by *category*. Use the [Per-phase quick-reference](#per-phase-quick-reference) table at the end to look up which targets are available for a specific phase.

**Variable overrides** are listed alongside the targets and tools that use them. Any variable can be set on the `make` command line:

```bash
make <target> VARIABLE=value
```

---

## Table of contents

1. [Docker container commands](#1-docker-container-commands)
2. [USB-Blaster management (usbipd-win)](#2-usb-blaster-management-usbipd-win)
3. [JTAG inspection](#3-jtag-inspection)
4. [Makefile targets — build pipeline](#4-makefile-targets--build-pipeline)
5. [Makefile targets — programming and deployment](#5-makefile-targets--programming-and-deployment)
6. [Makefile targets — debugging](#6-makefile-targets--debugging)
7. [Makefile targets — utilities](#7-makefile-targets--utilities)
8. [Quartus compilation tools](#8-quartus-compilation-tools)
9. [Platform Designer tools](#9-platform-designer-tools)
10. [Nios II toolchain](#10-nios-ii-toolchain)
11. [ARM toolchain](#11-arm-toolchain)
12. [OpenOCD](#12-openocd)
13. [Make variable overrides](#13-make-variable-overrides)
14. [Phase 11 — Ethernet LED server](#14-phase-11--ethernet-led-server)
15. [Per-phase quick reference](#15-per-phase-quick-reference)

---

## 1. Docker container commands

All build tools, FPGA compilers, and embedded toolchains run inside the `cvsoc/quartus:23.1` Docker image.

### Run a build (non-interactive)

```bash
docker run --rm \
  -v /path/to/cvsoc:/work \
  cvsoc/quartus:23.1 \
  bash -c "cd /work/<phase>/quartus && make all"
```

| Flag | Description |
|------|-------------|
| `--rm` | Remove the container automatically when the command exits |
| `-v /path/to/cvsoc:/work` | Bind-mount the repository root to `/work` inside the container |
| `bash -c "..."` | Run a shell command in the container |

### Run with USB/JTAG access (privileged)

Required for `nios2-download`, `jtagconfig`, and `openocd`:

```bash
docker run --rm --privileged \
  -v /path/to/cvsoc:/work \
  -v /path/to/cvsoc/common/docker/uname_shim.sh:/usr/local/bin/uname:ro \
  cvsoc/quartus:23.1 \
  <command>
```

| Flag | Description |
|------|-------------|
| `--privileged` | Grants the container access to USB devices on the host |
| `-v .../uname_shim.sh:/usr/local/bin/uname:ro` | Prevents Altera tools from misdetecting WSL2 inside Docker (see [uname shim](#uname-shim)) |

### Run the Quartus GUI (requires X11)

```bash
# On the host first — allow Docker containers to connect to your X server
xhost +local:docker

# Then launch Quartus
docker run --rm -it \
  --privileged --net=host \
  -v /tmp/.X11-unix:/tmp/.X11-unix \
  -e DISPLAY=$DISPLAY \
  -v $(pwd):/work \
  cvsoc/quartus:23.1 \
  /opt/intelFPGA/quartus/bin/quartus
```

### Launch Platform Designer (Qsys) GUI

```bash
docker run --rm -it \
  --privileged --net=host \
  -v /tmp/.X11-unix:/tmp/.X11-unix \
  -e DISPLAY=$DISPLAY \
  -v $(pwd):/work \
  cvsoc/quartus:23.1 \
  /opt/intelFPGA/quartus/sopc_builder/bin/qsys-edit
```

### uname shim

Altera shell scripts detect WSL2 by checking whether `uname -r` contains `microsoft`. Docker containers on WSL2 share the host kernel, so the check fires even inside the container, causing scripts to call `.exe` binaries that don't exist.

`common/docker/uname_shim.sh` wraps `/bin/uname` and strips `microsoft` from the output. It must be bind-mounted over `/usr/local/bin/uname` (which takes priority over `/bin/uname`).

---

## 2. USB-Blaster management (usbipd-win)

The USB-Blaster II can only be owned by one consumer at a time. `quartus_pgm.exe` runs on Windows and needs the cable detached from WSL2. `nios2-download` and OpenOCD run in Docker inside WSL2 and need it attached.

The Makefiles call `usbipd.exe` automatically. These commands are documented here for manual use and troubleshooting.

> **Run all `usbipd` commands from a WSL2 terminal**, not a Windows PowerShell.

### Installation (Windows — one time only)

```powershell
winget install --interactive --exact dorssel.usbipd-win
```

### Bind the device (Windows Administrator — one time only)

```powershell
usbipd bind --busid 2-4
```

Binding makes the device available for WSL2 attachment without needing Administrator rights each time. Verify with:

```powershell
usbipd list
```

The device should show `Shared` in the STATE column.

### Detach from WSL2 (give cable to Windows)

```bash
usbipd.exe detach --busid 2-4
```

Required before running `quartus_pgm.exe`.

### Attach to WSL2 (give cable to Docker)

```bash
usbipd.exe attach --wsl --busid 2-4
```

Required before running `nios2-download`, `jtagconfig`, or `openocd` in Docker.

### Override the bus ID

The default bus ID in all Makefiles is `2-4`. Override it per-command:

```bash
make program-sof USBIPD_BUSID=3-1 -C 04_nios2_led/quartus
```

---

## 3. JTAG inspection

### Enumerate the JTAG chain

```bash
docker run --rm --privileged \
  -v $(pwd)/common/docker/uname_shim.sh:/usr/local/bin/uname:ro \
  cvsoc/quartus:23.1 \
  bash -c "/opt/intelFPGA/quartus/bin/jtagd && sleep 2 && \
           /opt/intelFPGA/quartus/bin/jtagconfig"
```

Expected output for the DE10-Nano:

```
1) DE-SoC [1-1]
  4BA00477   SOCVHPS
  02D020DD   5CSEBA6(.|ES)/5CSEMA6/..
```

| Position | ID code | Device |
|----------|---------|--------|
| `@1` | `0x4BA00477` | HPS ARM DAP (Cortex-A9 debug access port) |
| `@2` | `0x02D020DD` | Cyclone V FPGA |

### Check USB device visibility

```bash
lsusb | grep -i altera
```

---

## 4. Makefile targets — build pipeline

Run `make <target>` from within the relevant `<phase>/quartus/` directory.

### `make all`

Full build pipeline from source to runnable binary. Available in every phase.

| Phase | Steps invoked |
|-------|---------------|
| 00, 01 | `project` → `compile` |
| 04, 06, 08 | `qsys` → `project` → `compile` → `bsp` → `app` |
| 05, 07, 09 | `qsys` → `project` → `compile` → `app` |

---

### `make qsys`

Generates the Platform Designer system from the TCL script, then runs HDL synthesis.

**Nios II phases (04, 06, 08):**

```bash
# Step 1 — generate .qsys from TCL
cd ../qsys && qsys-script --script=nios2_system.tcl

# Step 2 — generate VHDL netlist
qsys-generate nios2_system.qsys --synthesis=VHDL \
  --output-directory=../qsys/nios2_system_gen
```

**HPS phases (05, 07, 09):**

```bash
# Step 1 — generate .qsys from TCL
cd ../qsys && qsys-script --script=hps_system.tcl

# Step 2 — generate Verilog netlist
qsys-generate hps_system.qsys --synthesis=VERILOG
```

Followed automatically by `make patch-oct` (HPS phases only).

---

### `make patch-oct`

*(HPS phases: 05, 07, 09 only)*

Patches the generated DDR3 PHY Verilog to propagate the `USE_TERMINATION_CONTROL` parameter. This is required to avoid Fitter Error 174068 on Cyclone V.

```bash
python3 scripts/patch_oct.py \
  ../qsys/hps_system/synthesis/submodules/altdq_dqs2_acv_connect_to_hard_phy_cyclonev.sv
```

---

### `make project`

Creates the Quartus project (`.qpf`, `.qsf`) from the TCL script:

```bash
quartus_sh -t de10_nano_project.tcl
```

---

### `make compile`

**Nios II and pure-VHDL phases (00, 01, 04, 06, 08):** Single-pass compilation.

```bash
quartus_sh --flow compile <project_name> -c de10_nano
```

**HPS phases (05, 07, 09):** Two-pass compilation required for Cyclone V DDR3 I/O assignments.

```bash
# Pass 1 — synthesise and build timing netlist
quartus_map --read_settings_files=on --write_settings_files=off \
            <project_name> -c de10_nano

# Apply DDR3 I/O pin assignments from the generated TCL
quartus_sta -t .../submodules/hps_sdram_p0_pin_assignments.tcl de10_nano

# Pass 2 — re-synthesise with new pin assignments
quartus_map --read_settings_files=on --write_settings_files=off \
            <project_name> -c de10_nano
quartus_fit --read_settings_files=on --write_settings_files=off \
            <project_name> -c de10_nano
quartus_asm --read_settings_files=on --write_settings_files=off \
            <project_name> -c de10_nano
quartus_sta <project_name> -c de10_nano
```

---

### `make bsp`

*(Nios II phases: 04, 06, 08 only)*

Generates the Nios II HAL Board Support Package from the `.sopcinfo` system description.

```bash
nios2-bsp-create-settings \
  --sopc  ../qsys/nios2_system.sopcinfo \
  --type  hal \
  --settings ../software/bsp/settings.bsp \
  --bsp-dir  ../software/bsp \
  --script   /opt/intelFPGA/nios2eds/sdk2/bin/bsp-set-defaults.tcl \
  --cpu-name nios2

make -C ../software/bsp WINDOWS_EXE=
```

> `WINDOWS_EXE=` forces the generated BSP Makefile to use native Linux binaries instead of `.exe` binaries (which it would otherwise select when it detects a WSL2 kernel).

---

### `make app`

Compiles the C/assembly application. Delegates to the `software/app/Makefile`.

**Nios II (04, 06, 08):** Invokes `nios2-elf-gcc` directly.

```bash
make -C ../software/app
```

**HPS (05, 07, 09):** Passes the ARM cross-compiler explicitly.

```bash
make -C ../software/app CC=arm-linux-gnueabihf-gcc
```

Override the compiler:

```bash
make app ARM_CC=arm-linux-gnueabihf-gcc-6
```

---

### `make setup`

*(HPS phases: 05, 07, 09 only)*

Installs the ARM cross-compiler inside the Docker container from the Debian 9 snapshot archive. Only needed once per fresh container session.

```bash
echo "deb http://snapshot.debian.org/archive/debian/20220622T000000Z stretch main" \
  > /etc/apt/sources.list
apt-get update -o Acquire::Check-Valid-Until=false -qq
apt-get install -y -o Acquire::Check-Valid-Until=false \
  gcc-arm-linux-gnueabihf binutils-arm-linux-gnueabihf
```

Phase 09 also installs the GDB for ARM:

```bash
apt-get install -y -o Acquire::Check-Valid-Until=false gdb-arm-none-eabi
```

---

## 5. Makefile targets — programming and deployment

### `make program-sof`

Programs the FPGA bitstream via `quartus_pgm.exe` (Windows). On WSL2, automatically detaches the USB-Blaster from WSL2, programs, then re-attaches.

```bash
# Copy SOF to a Windows-native path (avoids UNC path rejection)
cp de10_nano.sof /mnt/c/Windows/Temp/de10_nano.sof

# Program via Windows Quartus Programmer, targeting device @2 (Cyclone V FPGA)
quartus_pgm.exe -m jtag -o "p;C:\Windows\Temp\de10_nano.sof@2"
```

Override the bus ID or programmer path:

```bash
make program-sof USBIPD_BUSID=3-1
make program-sof QUARTUS_PGM=/mnt/c/path/to/quartus_pgm.exe
```

> The `.sof` programs the FPGA's **volatile SRAM** configuration. The bitstream is lost on power cycle.

---

### `make download-elf` (Nios II phases)

Downloads and starts the Nios II ELF via `nios2-download` inside Docker.

```bash
docker run --rm --privileged \
  -v /path/to/cvsoc:/work \
  -v .../uname_shim.sh:/usr/local/bin/uname:ro \
  cvsoc/quartus:23.1 \
  nios2-download -g /work/<phase>/software/app/<name>.elf
```

| Flag | Description |
|------|-------------|
| `-g` | Start the CPU running immediately after download |

---

### `make download-elf` (HPS phases)

Loads the raw binary into HPS On-Chip RAM (OCRAM) at `0xFFFF0000` and starts execution via OpenOCD inside Docker.

```bash
docker run --rm --privileged \
  -v /path/to/cvsoc:/work \
  -v .../uname_shim.sh:/usr/local/bin/uname:ro \
  -v de10_nano_hps.cfg:/tmp/de10_nano_hps.cfg:ro \
  cvsoc/quartus:23.1 \
  bash -c '/opt/intelFPGA/quartus/bin/jtagd && sleep 2 && \
    /opt/intelFPGA/quartus/bin/openocd \
      -f /tmp/de10_nano_hps.cfg \
      -c "init" \
      -c "halt" \
      -c "load_image /work/<phase>/software/app/<name>.bin 0xFFFF0000 bin" \
      -c "resume 0xFFFF0000" \
      -c "shutdown"'
```

> The HPS target loads a **raw binary** (`.bin`), not the ELF. The Linux toolchain's ELF headers map segments below OCRAM, which causes a data abort when OpenOCD tries to load them. `objcopy -O binary` strips all ELF metadata.

---

### `make deploy-elf`

*(HPS phases: 05, 07, 09 only)*

Copies the ELF and raw binary to the board over SSH (requires the board to be booted into U-Boot or Linux).

```bash
scp hps_led.elf hps_led.bin root@192.168.1.100:/tmp/
```

Override the board's IP address or user:

```bash
make deploy-elf HPS_IP=10.0.0.42 HPS_USER=admin
```

Once copied, execute from U-Boot with:

```
# On the board (U-Boot prompt):
go 0xFFFF0000
```

---

### `make usb-windows` / `make usb-wsl`

Low-level targets that attach or detach the USB-Blaster. Normally called automatically by `program-sof`, `download-elf`, `terminal`, `gdb-server`, and `openocd`. Available for manual use during troubleshooting.

```bash
make usb-windows   # detach from WSL2, give to Windows
make usb-wsl       # attach to WSL2, give to Docker
```

---

## 6. Makefile targets — debugging

### `make terminal`

*(Nios II phases: 04, 06, 08)*

Opens the Nios II JTAG UART terminal to receive `printf` output from the firmware. Press **Ctrl-C** to exit.

```bash
docker run --rm -it --privileged \
  -v .../uname_shim.sh:/usr/local/bin/uname:ro \
  cvsoc/quartus:23.1 \
  nios2-terminal
```

---

### `make gdb-server`

*(Phase 08 — Nios II debug)*

Starts the `nios2-gdb-server` inside Docker. Run this in **Terminal 1**; leave it running while you start the GDB client in Terminal 2.

```bash
docker run --rm -it --privileged \
  --name nios2-gdb-server \
  -p 2345:2345 \
  -v /path/to/cvsoc:/work \
  -v .../uname_shim.sh:/usr/local/bin/uname:ro \
  cvsoc/quartus:23.1 \
  bash -c '/opt/intelFPGA/quartus/bin/jtagd && sleep 2 && \
    nios2-gdb-server \
      --tcpport 2345 \
      --tcppersist \
      --tcpdebug'
```

| Flag | Description |
|------|-------------|
| `--tcpport 2345` | TCP port the GDB server listens on |
| `--tcppersist` | Keep the server running after GDB disconnects |
| `--tcpdebug` | Print debug messages to help diagnose connection issues |

Override the default port:

```bash
make gdb-server GDB_PORT=3000
```

---

### `make gdb` (Nios II — phase 08)

Connects `nios2-elf-gdb` to the running `nios2-gdb-server` and executes the GDB init script. Run in **Terminal 2** while `gdb-server` is running in Terminal 1.

```bash
docker run --rm -it \
  --network host \
  -v /path/to/cvsoc:/work \
  cvsoc/quartus:23.1 \
  nios2-elf-gdb \
    -ex "set pagination off" \
    -x /work/08_nios2_debug/scripts/nios2_debug.gdb \
    /work/08_nios2_debug/software/app/nios2_debug.elf
```

| Flag | Description |
|------|-------------|
| `--network host` | Lets the container reach `localhost:2345` where the GDB server is listening |
| `-ex "set pagination off"` | Disable `--More--` prompts in the GDB output |
| `-x <script>` | Execute a GDB init script (sets breakpoints, defines helpers) |

---

### `make openocd`

*(Phase 09 — HPS debug)*

Starts OpenOCD as a GDB server for the HPS Cortex-A9. Run in **Terminal 1**.

```bash
docker run --rm -it --privileged \
  --name hps-openocd \
  -p 3333:3333 -p 4444:4444 \
  -v /path/to/cvsoc:/work \
  -v .../uname_shim.sh:/usr/local/bin/uname:ro \
  -v de10_nano_hps.cfg:/tmp/de10_nano_hps.cfg:ro \
  cvsoc/quartus:23.1 \
  bash -c '/opt/intelFPGA/quartus/bin/jtagd && sleep 2 && \
    /opt/intelFPGA/quartus/bin/openocd \
      -f /tmp/de10_nano_hps.cfg \
      -c "init" \
      -c "halt"'
```

| Port | Protocol | Description |
|------|----------|-------------|
| `3333` | GDB RSP | GDB client connects here |
| `4444` | Telnet | OpenOCD command interface |

Override the GDB port:

```bash
make openocd GDB_PORT=4444
```

---

### `make gdb` (HPS — phase 09)

Connects `arm-none-eabi-gdb` to the running OpenOCD server. Run in **Terminal 2**.

```bash
docker run --rm -it \
  --network host \
  -v /path/to/cvsoc:/work \
  cvsoc/quartus:23.1 \
  bash -c 'apt-get install -y gdb-arm-none-eabi -qq && \
    arm-none-eabi-gdb \
      -ex "set pagination off" \
      -x /work/09_hps_debug/scripts/hps_debug.gdb \
      /work/09_hps_debug/software/app/hps_debug.elf'
```

---

## 7. Makefile targets — utilities

### `make check_timing`

Checks the static timing analysis report for setup and hold slack violations.

```bash
python scripts/check_timing_slacks.py de10_nano.sta.rpt
```

The script lives in `00_led_blinking/scripts/` and is shared across all phases.

---

### `make clean`

Removes all generated files: Quartus databases, Platform Designer output, BSP, and compiled binaries.

```bash
rm -rf db incremental_db output_files
rm -rf ../qsys/nios2_system_gen ../qsys/nios2_system.qsys   # Nios II phases
rm -rf ../software/bsp                                        # Nios II phases
make -C ../software/app clean
rm -f *.rpt *.summary *.qsf *.qpf *.qws *.done *.smsg *.jdi *.pin *.sld *.sof dump.txt
```

---

## 8. Quartus compilation tools

These tools are invoked by `make compile` and its sub-targets. They are rarely called directly.

| Command | Purpose |
|---------|---------|
| `quartus_sh -t <script.tcl>` | Run a TCL script (used to create the project) |
| `quartus_sh --flow compile <proj> -c <rev>` | Full single-pass compilation (phases 00, 01, 04, 06, 08) |
| `quartus_map <proj> -c <rev>` | Analysis and synthesis (converts HDL to a netlist) |
| `quartus_fit <proj> -c <rev>` | Fitter — place and route |
| `quartus_asm <proj> -c <rev>` | Assembler — generates the `.sof` bitstream |
| `quartus_sta <proj> -c <rev>` | Static Timing Analyser |
| `quartus_sta -t <script.tcl> <rev>` | Run a TCL script inside the STA context (applies DDR3 pin assignments) |

The `--read_settings_files=on --write_settings_files=off` flags, used in the HPS two-pass flow, ensure each tool reads the QSF but does not overwrite it mid-flow.

---

## 9. Platform Designer tools

### `qsys-script`

Executes a TCL script inside the Platform Designer (Qsys) engine to generate a `.qsys` system description file.

```bash
cd ../qsys && qsys-script --script=nios2_system.tcl
```

Must be run from the directory containing the `.tcl` file; `save_system` in the script uses a local relative path.

### `qsys-generate`

Generates synthesisable HDL from a `.qsys` file.

```bash
# Nios II — generates VHDL
qsys-generate nios2_system.qsys \
  --synthesis=VHDL \
  --output-directory=../qsys/nios2_system_gen

# HPS — generates Verilog
qsys-generate hps_system.qsys --synthesis=VERILOG
```

---

## 10. Nios II toolchain

### `nios2-bsp-create-settings`

Creates a HAL BSP `settings.bsp` file from a `.sopcinfo` system description. Preferred over the `nios2-bsp` wrapper inside Docker on WSL2 hosts.

```bash
nios2-bsp-create-settings \
  --sopc   <path>.sopcinfo \
  --type   hal \
  --settings <bsp-dir>/settings.bsp \
  --bsp-dir  <bsp-dir> \
  --script   /opt/intelFPGA/nios2eds/sdk2/bin/bsp-set-defaults.tcl \
  --cpu-name nios2
```

### `nios2-elf-gcc`

Nios II cross-compiler. The application Makefile invokes it directly.

```
nios2-elf-gcc -DALT_SINGLE_THREADED=1 -DNIOS2 \
  -I<bsp-dir> -I<bsp-dir>/HAL/inc -I<bsp-dir>/drivers/inc \
  -Os -g -Wall -Wextra \
  -mno-hw-div -mno-hw-mul \
  -T<bsp-dir>/linker.x -L<bsp-dir> -lhal_bsp \
  -Wl,--gc-sections \
  main.c -o <name>.elf
```

Debug builds use `-O0 -g3` in place of `-Os -g`.

### `nios2-elf-size`

Prints the memory footprint of a compiled Nios II ELF.

```bash
nios2-elf-size nios2_led.elf
```

### `nios2-download`

Downloads a Nios II ELF to the soft-core via JTAG and optionally starts execution.

```bash
nios2-download -g /path/to/<name>.elf
```

| Flag | Description |
|------|-------------|
| `-g` | Start the CPU running immediately after download |

### `nios2-terminal`

Opens the JTAG UART terminal to receive `printf` output. Press **Ctrl-C** to exit.

```bash
nios2-terminal
```

### `nios2-gdb-server`

Bridges JTAG to the GDB remote serial protocol over TCP.

```bash
nios2-gdb-server \
  --tcpport 2345 \
  --tcppersist \
  --tcpdebug
```

### `nios2-elf-gdb`

GDB for the Nios II architecture. Connects to a running `nios2-gdb-server`.

```bash
nios2-elf-gdb \
  -ex "set pagination off" \
  -x scripts/nios2_debug.gdb \
  software/app/nios2_debug.elf
```

---

## 11. ARM toolchain

### `arm-linux-gnueabihf-gcc`

ARM Cortex-A9 cross-compiler (hard-float ABI). Used for all HPS bare-metal applications.

```
arm-linux-gnueabihf-gcc \
  -mcpu=cortex-a9 -mfpu=neon-vfpv4 -mfloat-abi=hard \
  -ffreestanding -nostdlib \
  -Os -g -Wall -Wextra \
  -T linker.ld -nostdlib -Wl,--build-id=none \
  startup.S main.c -o hps_led.elf
```

Debug builds use `-O0 -g3` in place of `-Os -g`.

| Flag | Description |
|------|-------------|
| `-mcpu=cortex-a9` | Target the Cyclone V HPS Cortex-A9 |
| `-mfpu=neon-vfpv4` | Enable NEON/VFPv4 FPU |
| `-mfloat-abi=hard` | Pass float arguments in VFP registers |
| `-ffreestanding` | No C standard library startup assumptions |
| `-nostdlib` | Do not link against libc or libgcc |
| `-Os` | Optimise for size (fits the 64 KB OCRAM) |
| `-O0 -g3` | No optimisation; maximum debug info (debug phases) |
| `-T linker.ld` | Specify the linker script (maps code to `0xFFFF0000`) |

### `arm-linux-gnueabihf-objcopy`

Converts the ELF to a flat binary for loading via OpenOCD.

```bash
arm-linux-gnueabihf-objcopy -O binary hps_led.elf hps_led.bin
```

### `arm-linux-gnueabihf-size`

Prints the memory footprint of a compiled ARM ELF.

```bash
arm-linux-gnueabihf-size hps_led.elf
```

### `arm-none-eabi-gdb`

Bare-metal ARM GDB. Connects to the OpenOCD GDB server.

```bash
arm-none-eabi-gdb \
  -ex "set pagination off" \
  -x scripts/hps_debug.gdb \
  software/app/hps_debug.elf
```

---

## 12. OpenOCD

Intel's OpenOCD (`/opt/intelFPGA/quartus/bin/openocd`) uses the `aji_client` interface to communicate with `jtagd` over a local socket. It does **not** include the generic `usb_blaster` driver.

### `jtagd`

Altera JTAG daemon. Must be started before OpenOCD or `jtagconfig` can access the cable.

```bash
/opt/intelFPGA/quartus/bin/jtagd && sleep 2
```

The `sleep 2` allows the daemon to initialise fully before the next command.

### `openocd`

Runs a series of commands against the target. All HPS targets use the board config file `scripts/de10_nano_hps.cfg`.

**Load binary and run (non-interactive):**

```bash
/opt/intelFPGA/quartus/bin/openocd \
  -f /path/to/de10_nano_hps.cfg \
  -c "init" \
  -c "halt" \
  -c "load_image /path/to/app.bin 0xFFFF0000 bin" \
  -c "resume 0xFFFF0000" \
  -c "shutdown"
```

**Start GDB server (interactive — phase 09):**

```bash
/opt/intelFPGA/quartus/bin/openocd \
  -f /path/to/de10_nano_hps.cfg \
  -c "init" \
  -c "halt"
```

OpenOCD then listens on:
- Port `3333` for GDB (RSP protocol)
- Port `4444` for a Telnet command interface

| OpenOCD command | Description |
|-----------------|-------------|
| `init` | Initialise the JTAG interface and connect to the target |
| `halt` | Halt all CPU cores |
| `load_image <file> <addr> bin` | Write a raw binary to the target at the given address |
| `resume <addr>` | Start execution at the given address |
| `shutdown` | Disconnect and exit OpenOCD |

---

## 13. Make variable overrides

All variables have defaults in each Makefile and can be overridden on the command line.

| Variable | Default | Description | Phases |
|----------|---------|-------------|--------|
| `USBIPD_BUSID` | `2-4` | USB bus ID of the USB-Blaster II | 04–09 |
| `USBIPD` | `usbipd.exe` | Path to `usbipd-win` executable | 04–09 |
| `QUARTUS_PGM` | `/mnt/c/intelFPGA_lite/23.1std/qprogrammer/bin64/quartus_pgm.exe` | Path to `quartus_pgm.exe` | 04–09 |
| `DOCKER_IMAGE` | `cvsoc/quartus:23.1` | Docker image for build and debug containers | 04–11 |
| `ARM_CC` | `arm-linux-gnueabihf-gcc` | ARM cross-compiler | 05, 07, 09, 11 |
| `HPS_IP` | `192.168.1.100` | Board IP address for `deploy-elf` (SSH) | 05, 07, 09 |
| `HPS_USER` | `root` | SSH user for `deploy-elf` | 05, 07, 09 |
| `GDB_PORT` | `2345` (Nios II) / `3333` (HPS) | TCP port for GDB server | 08, 09 |
| `SDCARD` | *(required)* | SD card block device for `make flash` | 10, 11 |
| `BOARD_HOST` | *(required)* | Board IPv6/IPv4 address (with zone ID for link-local) | 11 |
| `BOARD_PORT` | `5005` | UDP port of the LED server | 11 |

---

## 14. Phase 11 — Ethernet LED server

All targets in this section are invoked from the `11_ethernet_hps_led/` directory (no `quartus/` subdirectory — this phase has no Quartus project).

```bash
cd 11_ethernet_hps_led
make <target>
```

### `make all`

Runs `rbf` then `buildroot`: copies the Phase 6 bitstream and builds the complete Linux SD card image.

---

### `make rbf`

Copies the compressed FPGA bitstream from Phase 6 (`10_linux_led/de10_nano.rbf`) to the local directory. Does **not** run Quartus or Docker — Phase 6 must have already produced the `.rbf`.

```bash
make rbf
```

If `10_linux_led/de10_nano.rbf` is missing:

```bash
cd ../10_linux_led && make rbf   # requires cvsoc/quartus:23.1 Docker image
```

---

### `make buildroot-download`

Downloads and extracts Buildroot 2024.11.1 to `buildroot-2024.11.1/`. Safe to run repeatedly — skipped if the directory already exists.

```bash
make buildroot-download
```

---

### `make buildroot-config`

Applies `de10_nano_defconfig` to the Buildroot source tree with `BR2_EXTERNAL` pointing to `br2-external/`.

```bash
make buildroot-config
```

---

### `make buildroot`

Full Buildroot build: cross-compiler, Linux kernel, U-Boot, BusyBox, `led_server`, Dropbear, `fpga_load.ko`, and the SD card image. Calls `rbf` and `buildroot-config` automatically. Runs `led-server-dirclean` before building to pick up source changes.

```bash
make buildroot
```

> **Duration:** 15–30 minutes on first run. Subsequent runs that only change `led_server.c` are faster with `make server-cross` + `scp` (see below).

Output: `buildroot-2024.11.1/output/images/sdcard.img`

---

### `make server-cross`

Standalone ARM cross-compile of `led_server.c` using `arm-linux-gnueabihf-gcc` inside the `cvsoc/quartus:23.1` Docker container. Faster than a full Buildroot rebuild for iterative development.

```bash
# 🐳 Runs inside Docker
make server-cross
```

Output: `software/led_server/led_server` (ARM ELF, ready to `scp` to the board)

Deploy to the running board:

```bash
BOARD="root@fe80::...%enx..."
ssh "$BOARD" "/etc/init.d/S40led_server stop"
scp software/led_server/led_server "$BOARD:/tmp/led_server"
ssh "$BOARD" "mv /tmp/led_server /usr/bin/led_server && chmod +x /usr/bin/led_server && /etc/init.d/S40led_server start"
```

Override the compiler:

```bash
make server-cross ARM_CC=buildroot-2024.11.1/output/host/bin/arm-linux-gnueabihf-gcc
```

---

### `make test`

Runs the Python protocol unit tests in `client/test_protocol.py`. No board or network connection required.

```bash
make test
```

---

### `make flash`

Writes `sdcard.img` to a physical SD card. Requires `SDCARD` to be set. Prints a 5-second countdown before writing.

```bash
make flash SDCARD=/dev/sdX
# Or:
sudo dd if=buildroot-2024.11.1/output/images/sdcard.img of=/dev/sdX bs=4M status=progress conv=fsync
```

---

### `make clean`

Removes `de10_nano.rbf` and the entire `buildroot-*` directory.

```bash
make clean
```

---

### `make help`

Prints a summary of all targets and the expected client command.

---

### PC client — `send_led_pattern.py`

The Python client lives in `client/`. It supports IPv4, IPv6 global, and link-local addresses with zone IDs.

```bash
# Set a specific 8-bit LED pattern
python3 client/send_led_pattern.py --host <board-addr> --pattern 0xA5

# Read the current LED state
python3 client/send_led_pattern.py --host <board-addr> --get

# Run a named animation (loops until Ctrl+C)
python3 client/send_led_pattern.py --host <board-addr> --animate chase
python3 client/send_led_pattern.py --host <board-addr> --animate breathe --speed 200

# Link-local IPv6 (WSL2 — include zone ID with % separator)
python3 client/send_led_pattern.py \
    --host "fe80::2833:8aff:fe95:cb3d%enx08beac224c03" \
    --pattern 0xFF
```

| Flag | Default | Description |
|------|---------|-------------|
| `--host` | *(required)* | Board IP address, hostname, or `fe80::...%iface` link-local |
| `--port` | `5005` | UDP port |
| `--pattern` | — | Hex LED pattern to SET, e.g. `0xA5` |
| `--get` | — | Read current pattern |
| `--animate` | — | Named animation: `chase`, `breathe`, `blink`, `stripes`, `all` |
| `--speed` | `100` | Animation step interval in milliseconds |

---

## 15. Per-phase quick reference

| Target | 00 | 01 | 04 | 05 | 06 | 07 | 08 | 09 | 10 | 11 |
|--------|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|
| `all` | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| `clean` | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| `project` | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — | — |
| `compile` | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — | — |
| `check_timing` | ✓ | — | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — | — |
| `setup` | — | — | — | ✓ | — | ✓ | — | ✓ | — | — |
| `qsys` | — | — | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — | — |
| `patch-oct` | — | — | — | ✓ | — | ✓ | — | ✓ | — | — |
| `bsp` | — | — | ✓ | — | ✓ | — | ✓ | — | — | — |
| `app` | — | — | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — | — |
| `program-sof` | — | — | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — | — |
| `download-elf` | — | — | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — | — |
| `deploy-elf` | — | — | — | ✓ | — | ✓ | — | ✓ | — | — |
| `terminal` | — | — | ✓ | — | ✓ | — | ✓ | — | — | — |
| `usb-windows` | — | — | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — | — |
| `usb-wsl` | — | — | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — | — |
| `gdb-server` | — | — | — | — | — | — | ✓ | — | — | — |
| `gdb` | — | — | — | — | — | — | ✓ | ✓ | — | — |
| `openocd` | — | — | — | — | — | — | — | ✓ | — | — |
| `rbf` | — | — | — | — | — | — | — | — | ✓ | ✓ |
| `buildroot-download` | — | — | — | — | — | — | — | — | ✓ | ✓ |
| `buildroot-config` | — | — | — | — | — | — | — | — | ✓ | ✓ |
| `buildroot` | — | — | — | — | — | — | — | — | ✓ | ✓ |
| `app-cross` | — | — | — | — | — | — | — | — | ✓ | — |
| `server-cross` | — | — | — | — | — | — | — | — | — | ✓ |
| `test` | — | — | — | — | — | — | — | — | — | ✓ |
| `flash` | — | — | — | — | — | — | — | — | ✓ | ✓ |

### Phase descriptions

| Phase | Project | CPU | Description |
|-------|---------|-----|-------------|
| `00_led_blinking` | Pure VHDL | — | All LEDs blink in sync via a VHDL counter |
| `01_led_running` | Pure VHDL | — | Running LED pattern (shift register) |
| `04_nios2_led` | Nios II | Nios II/e | Soft-core CPU drives LED patterns over Avalon-MM |
| `05_hps_led` | HPS bare-metal | Cortex-A9 | ARM bare-metal app drives LEDs via LW H2F bridge |
| `06_nios2_interrupts` | Nios II | Nios II/e | Button PIO generates IRQs handled by Nios II ISR |
| `07_hps_interrupts` | HPS bare-metal | Cortex-A9 | FPGA-to-HPS IRQ routed through GIC to ARM ISR |
| `08_nios2_debug` | Nios II debug | Nios II/e | GDB debugging via `nios2-gdb-server` over JTAG |
| `09_hps_debug` | HPS debug | Cortex-A9 | GDB debugging via OpenOCD `aji_client` over JTAG |
| `10_linux_led` | Embedded Linux | Cortex-A9 (Linux) | Buildroot Linux image; FPGA programmed at boot via kernel module; LED control from user-space C and Python via `mmap(/dev/mem)` |
| `11_ethernet_hps_led` | Ethernet LED control | Cortex-A9 (Linux) | Buildroot Linux image with UDP `led_server` + Dropbear SSH; LED patterns sent over UDP from a PC Python client |
