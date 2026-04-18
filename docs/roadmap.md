# Project Roadmap

> Last updated: April 2026

This roadmap describes the planned development of the `cvsoc` tutorial series. It is organized into phases that mirror the learning progression described in `about.md`, with cross-cutting quality tracks that run through all phases.

---

## Guiding Principles

- **Incremental complexity** — each phase builds directly on the previous one. No phase skips infrastructure.
- **Practice what we preach** — CI/CD, simulation, and clean coding are applied to the repo itself, not just described.
- **Reproducibility first** — every phase must be fully buildable from a clean clone with documented tooling.
- **Consistency** — all projects follow the same directory layout and Makefile conventions.
- **Scripting-based** — every step of every phase must be executable from the command line without opening a GUI. `make all` from the project directory is the single entry point for any build, simulation, or generation step. No manual GUI interaction is ever required or assumed.

---

## Scripting Philosophy

The entire series — from FPGA synthesis to Linux image creation — is designed to be **100% script-driven**. This makes every project:

- **CI/CD-compatible**: any step that a human can run, the pipeline can run.
- **Reproducible**: a fresh `git clone` followed by `make all` produces an identical result on any machine.
- **Teachable**: scripts are documentation. Reading the Makefile or TCL script tells you exactly what the tool does.

The table below maps each phase to its key CLI tools. No GUI is required at any point.

| Phase | Key CLI Tools | What They Script |
|-------|--------------|-----------------|
| 0–1 FPGA synthesis | `quartus_sh`, `quartus_map`, `quartus_fit`, `quartus_asm`, `quartus_sta` | Full Quartus compilation flow |
| 0–1 Simulation | `ghdl`, `vunit` (Python runner), `pytest` (cocotb) | VHDL analysis, elaboration, simulation |
| 0–1 Project creation | `quartus_sh -t project.tcl` | Generates `.qsf`/`.qpf` from TCL |
| 0–1 Timing check | `python check_timing_slacks.py` | Parses STA report, fails on violations |
| 2 Qsys generation | `qsys-script --script=system.tcl` | Creates full Platform Designer system |
| 2 Qsys compilation | `qsys-generate system.qsys --synthesis=VHDL` | Generates HDL from Qsys system |
| 2 Nios II BSP | `nios2-bsp hal bsp --sopcinfo=system.sopcinfo` | Generates Board Support Package |
| 2 Nios II app | `make -C software/app/` | Cross-compiles C application |
| 2 JTAG programming | `quartus_pgm -m jtag -o "p;design.sof"` | Programs FPGA over JTAG |
| 3 HPS preloader | `bsp-create-settings --type spl ...` + `make` | Generates and builds U-Boot SPL |
| 3 ARM cross-compile | `arm-linux-gnueabihf-gcc` via `Makefile` | Builds bare-metal ARM application |
| 3 HPS programming | `quartus_hps -c 1 -o GDBSERVER` | Programs HPS via JTAG |
| 5 Nios II GDB | `nios2-gdb-server --tcpport 2345` | Remote GDB bridge for Nios II |
| 5 ARM GDB | `arm-linux-gnueabihf-gdb` via `OpenOCD` | Remote GDB for ARM Cortex-A9 |
| 6 Linux build | `make de10_nano_defconfig && make` (Buildroot) | Full OS image: kernel, rootfs, U-Boot |
| 6 Device tree | `dtc -I dts -O dtb overlay.dts -o overlay.dtbo` | Compiles device tree overlay |
| 6 FPGA bitstream load | `cp design.rbf /lib/firmware/ && echo 1 > /sys/class/fpga_manager/...` | Loads bitstream from Linux |
| 7 UDP server | `make -C software/server/` | Builds Linux UDP server application |
| 7 PC client | `python send_led_pattern.py --host <ip> --pattern 0xA5` | Sends commands from PC |

> **One unavoidable manual step:** writing the SD card image to physical media  
> (`dd if=sdcard.img of=/dev/sdX bs=4M status=progress`) requires a human to  
> plug in the card. Everything else runs unattended.

---

## Current State (Baseline)

| Project | Status | Description |
|---------|--------|-------------|
| `00_led_blinking` | ✅ Done | Pure VHDL, all 8 LEDs blink at 1 Hz |
| `01_led_running` | ✅ Done | Pure VHDL, single running-light shifts across 8 LEDs |
| Shared `common/ip/` | ✅ Done | `power_on_reset_generator` reusable component |
| Simulation | ❌ Missing | No testbenches exist |
| CI/CD | ❌ Missing | No pipeline configuration |
| Dev environment docs | ❌ Missing | No setup guide, no Dockerfile |

---

## Phase 0 — Infrastructure & Quality Baseline

**Goal:** Make the existing two projects consistent, fully reproducible, and automatically verified. This phase establishes the scaffolding every subsequent phase depends on.

### 0.1 Development Environment Documentation
- Write `docs/setup/windows.md` — Quartus Prime installation, Nios2 Command Shell usage, Python requirements
- Write `docs/setup/linux.md` — Quartus Prime installation on Ubuntu/Debian, environment variables
- Write `docs/setup/docker.md` — usage guide for the Docker image
- Create `Dockerfile` at repo root with Quartus Lite + GHDL + Python pre-installed
- Document the minimum required Quartus Prime version

### 0.2 Repository Consistency Fixes
- Add `doc/README.md` to `01_led_running` matching the structure of `00_led_blinking`
- Add `scripts/check_timing_slacks.py` to `01_led_running` (or factor into `common/scripts/`)
- Add `check_timing` target to `01_led_running/quartus/Makefile`
- Move `de10_nano_pin_assignments.tcl` and `de10_nano.sdc` to `common/board/de10_nano/` and source them from both projects
- Fix the one-cycle LED-off glitch in `led_running.vhd` (check `x"80"` before shift, not `x"00"` after)
- Add a root `Makefile` with `all`, `clean`, and per-project targets

### 0.3 Simulation Infrastructure
- Simulation framework: **GHDL + VUnit** — both are CLI-only tools, no GUI required
- Add `sim/` directory to both `00_led_blinking` and `01_led_running`
- Each testbench is a standard VHDL file; the VUnit Python runner discovers and executes them:
  ```sh
  python sim/run.py          # run all testbenches
  python sim/run.py -v       # verbose output with waveform export (.vcd)
  ```
- `make sim` in each project directory invokes the VUnit runner
- Write testbench for `power_on_reset_generator` (verify reset duration in cycles)
- Write testbench for `led_blinking` (verify toggle frequency using VUnit assertions)
- Write testbench for `led_running` (verify shift pattern and wrap-around behavior)

### 0.4 CI/CD Pipeline (GitHub Actions)
- Create `.github/workflows/build.yml`
  - Triggers: push to `master`, pull requests
  - Jobs: lint VHDL (vhdl-linter or NVC), run simulations (GHDL + VUnit), check timing reports
- Create `.github/workflows/docker.yml`
  - Builds and publishes Docker image to Docker Hub on tagged releases
- Add status badges to `README.md`

---

## Phase 1 — VHDL-Only LED Effects (Extended)

**Goal:** Expand the pure-VHDL chapter with more complex designs, deeper simulation, and cocotb introduction. Provides richer teaching material before introducing software.

### 1.1 Additional LED Patterns
- `02_led_breathing` — PWM-based brightness control simulating a breathing effect
  - Teaches: PWM generation, duty cycle, counter hierarchies
- `03_led_knight_rider` — back-and-forth running light (Knight Rider / KITT pattern)
  - Teaches: state machines (FSM), direction control, boundary detection

### 1.2 Deeper Simulation Coverage
- Add cocotb testbench alongside VUnit for `led_running` (Python-based simulation)
- Add waveform export and documentation on viewing `.vcd` files with GTKWave
- Demonstrate simulation-driven design: write testbench first, then implement

### 1.3 Timing and Synthesis Analysis
- Expand `check_timing_slacks.py` into a more complete report parser
- Add resource utilization summary (LUT/FF counts) to the post-build output
- Document how to interpret Quartus Compilation reports

---

## Phase 2 — Nios II Soft CPU (Bare Metal)

**Goal:** Introduce Intel's Nios II soft-processor. Students build an Avalon MM Slave peripheral in VHDL and control it from C firmware.

### 2.1 Nios II System with Platform Designer (Qsys)
- `04_nios2_led` — Create a Platform Designer system including:
  - Nios II/e (economy) CPU
  - On-chip memory (32 KB)
  - JTAG UART (for `printf` output)
  - Custom Avalon MM Slave: LED controller register
- The entire Qsys system is defined in a TCL script (`system.tcl`) and generated via:
  ```sh
  qsys-script --script=system.tcl           # create/update the .qsys file
  qsys-generate system.qsys --synthesis=VHDL  # generate HDL output
  ```
- No Platform Designer GUI interaction required; the TCL script is the source of truth

### 2.2 Custom Avalon MM Slave Peripheral
- Implement `led_controller_avmm.vhd`:
  - 32-bit Avalon MM Slave with one write register for LED pattern
  - One read register for LED status
  - Clear documentation of the register map
- Write complete testbench for the Avalon MM Slave (protocol compliance)
- Add to `common/ip/` for reuse in Phase 3

### 2.3 Nios II Bare Metal Application
- BSP generation via `nios2-bsp hal bsp --sopcinfo=system.sopcinfo --cpu-name=nios2`
- C application that cycles LED patterns through the custom peripheral
- Full build from a single `make` call: BSP → application → `.elf`
- JTAG programming via `quartus_pgm -m jtag -o "p;output_files/design.sof"` then `nios2-download -g app.elf`

### 2.4 Documentation
- `04_nios2_led/doc/README.md`: full walkthrough of Qsys flow, Avalon MM protocol, and BSP setup

---

## Phase 3 — ARM HPS (Bare Metal)

**Goal:** Use the Hard Processor System (ARM Cortex-A9) on the Cyclone V SoC. Students access the FPGA fabric via the HPS-to-FPGA Lightweight AXI-to-Avalon bridge.

### 3.1 HPS-FPGA Integration
- `05_hps_led` — Platform Designer system including:
  - HPS component (ARM Cortex-A9)
  - Lightweight HPS-to-FPGA AXI bridge
  - LED Controller Avalon MM Slave (reused from Phase 2)
- Document the HPS pin assignments and SDRAM interface constraints

### 3.2 Bare Metal ARM Application
- ARM bare-metal C application (no OS):
  - Memory-mapped I/O directly to the FPGA peripheral base address
  - Cyclic LED pattern with configurable speed
- Cross-compilation via `arm-linux-gnueabihf-gcc` driven by `Makefile`
- U-Boot SPL (preloader) generated entirely via CLI:
  ```sh
  bsp-create-settings --type spl --bsp-dir preloader/ \
    --preloader-settings-dir hps_isw_handoff/
  make -C preloader/
  ```
- SD card image assembled by a shell script: `scripts/make_sdcard_image.sh`
- JTAG load via `quartus_pgm` (no SD card needed for development iteration)

### 3.3 Documentation
- `05_hps_led/doc/README.md`: HPS architecture overview, bridge addressing, bare metal startup sequence

---

## Phase 4 — Hardware Interrupts (Nios II & HPS)

**Goal:** Master the interrupt subsystems of both the Nios II and the ARM HPS. Students will trigger CPU actions using physical push buttons on the DE10-Nano.

### 4.1 Qsys Evolution: Adding Button PIO
- Update both Nios II and HPS Qsys systems to include a 2-bit PIO component for the push buttons (`KEY[0]` and `KEY[1]`).
- Enable "Interrupt" mode in the PIO component:
  - Generate IRQ on edge (falling edge for active-low buttons).
  - Connect the IRQ line to the CPU (Nios II Internal Interrupt Controller or HPS GIC).
- Pin assignments (defined in TCL):
  - `KEY[0]`: `PIN_AH17` (3.3-V LVTTL)
  - `KEY[1]`: `PIN_AH16` (3.3-V LVTTL)

### 4.2 Nios II Interrupt Handling
- `06_nios2_interrupts` — C application using the HAL (Hardware Abstraction Layer) interrupt API.
- Implement a Register-Based ISR:
  - Button press toggles a software state or increments a counter.
  - Main loop displays the state on the LEDs.
- Teaches: `alt_ic_isr_register()`, interrupt context vs. main context, volatile variables.

### 4.3 ARM HPS Interrupt Handling (GIC)
- `07_hps_interrupts` — Bare-metal ARM application.
- Configure the Generic Interrupt Controller (GIC):
  - Map the FPGA-to-HPS interrupt line (e.g., IRQ 72).
  - Setup the Vector Table and Exception Handlers in `startup.S`.
- Teaches: GIC distributor and CPU interface configuration, ARM exception modes (IRQ/SVC), stack initialization.

### 4.4 Documentation
- `06_nios2_interrupts/doc/README.md` and `07_hps_interrupts/doc/README.md`: Interrupt latency, race conditions, and debouncing strategies.

---

## Phase 5 — Software Debugging (GDB)

**Goal:** Master remote debugging techniques using GDB for both the Nios II soft-processor and the ARM HPS.

### 5.1 Nios II Debugging with nios2-gdb-server
- Use `nios2-gdb-server` to create a JTAG-to-GDB bridge.
- Connect from `nios2-elf-gdb` (or generic `gdb`) to perform source-level debugging.
- Teaches: setting hardware breakpoints, inspecting Avalon peripheral registers via memory-mapped I/O, backtracing from exception handlers.

### 5.2 ARM HPS Debugging with OpenOCD
- Use Intel's OpenOCD (via `aji_client`) as a GDB server for the ARM Cortex-A9.
- Connect from `arm-linux-gnueabihf-gdb` to debug bare-metal applications.
- Teaches: debugging the `startup.S` boot sequence, inspecting GIC (Generic Interrupt Controller) registers, using watchpoints to catch data corruption.

### 5.3 Documentation
- `04_nios2_led/doc/debugging.md` and `05_hps_led/doc/debugging.md`: Step-by-step GDB workflows, common GDB commands for embedded systems.

---

## Phase 6 — Embedded Linux on HPS

**Goal:** Boot Linux on the ARM HPS and control FPGA LEDs from a proper Linux device driver and user-space application.

### 6.1 Linux Build Environment
- Build system: **Buildroot** — entirely Makefile-driven, no GUI required
  ```sh
  make BR2_EXTERNAL=../br2-external de10_nano_defconfig
  make        # produces output/images/sdcard.img
  ```
- All kernel config, U-Boot config, and package selection managed via `defconfig` files stored in the repo
- SD card image written via `dd` (the one manual hardware step in the entire series)

### 6.2 Device Tree Overlay
- Device tree overlay source (`.dts`) stored in the repo under `08_linux_led/dts/`
- Compiled via:
  ```sh
  dtc -I dts -O dtb -o fpga_led.dtbo fpga_led.dts
  ```
- FPGA bitstream loaded from Linux via FPGA Manager (no Quartus GUI on the host PC):
  ```sh
  cp design.rbf /lib/firmware/
  echo 1 > /sys/class/fpga_manager/fpga0/flags
  echo design.rbf > /sys/class/fpga_manager/fpga0/firmware
  ```

### 6.3 Linux Kernel Driver
- `08_linux_led` — UIO (Userspace I/O) driver approach (simpler) as first step
- Optionally: full `platform_driver` in-kernel module as advanced variant
- Sysfs interface for LED pattern control: `echo 0xAA > /sys/class/leds/fpga_led/pattern`

### 6.4 User-Space Application
- C application using `/dev/uioX` or sysfs to animate LEDs
- Python script alternative using mmap for direct register access
- Demonstrate the full software stack from user space to FPGA fabric

### 6.5 Documentation
- `08_linux_led/doc/README.md`: Linux boot sequence on DE10-Nano, device tree, driver model overview

---

## Phase 7 — Ethernet Control

**Goal:** Control the FPGA LEDs from a PC over the network. Demonstrates the complete hardware+software stack.

### 7.1 Network Stack Choice
Two sub-tracks (implement one or both):
- **Track A: HPS Ethernet** — Use the on-chip HPS Gigabit Ethernet MAC with Linux networking stack. Simplest approach, highest-level abstraction.
- **Track B: FPGA Soft MAC** — Implement a lightweight UDP listener in the FPGA fabric (using an open-source Ethernet IP core). Lower-level, teaches network protocols in HDL.

### 7.2 Track A — HPS Ethernet Server
- `09_ethernet_hps_led` — Linux application acting as UDP server
  - Receives LED pattern commands as UDP packets
  - Forwards commands to FPGA LED controller via sysfs/UIO
- PC-side Python client script: `send_led_pattern.py --host <ip> --pattern 0xA5`
- Document DE10-Nano Ethernet port configuration and static IP setup

### 7.3 Track B — FPGA UDP Receiver (Advanced)
- `09_ethernet_fpga_led` — Pure FPGA Ethernet implementation
  - Integrate open-source Ethernet MAC (e.g., LiteEth or Tri-Speed Ethernet IP)
  - Implement minimal UDP/IP stack in VHDL
  - Parse incoming UDP packets to extract LED commands
  - Drive LED controller directly from FPGA logic
- Full simulation of UDP packet reception using cocotb

### 7.4 Documentation
- Protocol specification: packet format, port numbers, command encoding
- Network setup guide and PC-side tool usage

---

## Cross-Cutting Track: Quality & Tooling (Ongoing)

These improvements are not tied to a single phase and should progress in parallel.

### Simulation Coverage Target
| Phase Completion | Target Simulation Coverage |
|------------------|-----------------------------|
| Phase 0          | All existing IPs have testbenches |
| Phase 1          | All new IPs have testbenches |
| Phase 2+         | Avalon MM Slave compliance tests |
| Phase 7+         | Co-simulation (cocotb + Python) for HPS interfaces |

### CI/CD Evolution
| Milestone | CI/CD Addition |
|-----------|----------------|
| Phase 0   | GHDL simulation on every PR |
| Phase 2   | Nios II BSP compilation check |
| Phase 3+  | ARM cross-compilation check |
| Phase 7+  | Buildroot/Yocto build verification |

### Documentation Standards
- Every project directory must contain `doc/README.md` before being merged
- All new VHDL IP must include a register map table (if applicable)
- All new IP must include a block diagram or ASCII art signal flow in its documentation
- Every `doc/README.md` must include a **"How to Build"** section where every command is a literal CLI command — no step may say "open the GUI and click..."

---

## Project Numbering Convention

| Range     | Category |
|-----------|----------|
| `00–03`   | Pure VHDL / FPGA-only designs |
| `04`      | Nios II soft CPU |
| `05`      | ARM HPS bare metal |
| `06–07`   | Hardware Interrupts (Nios II & HPS) |
| `08–09`   | Software Debugging (GDB) |
| `10`      | Embedded Linux |
| `11`      | Ethernet / Networking |
| `12+`     | Reserved for future extensions |

---

## Dependencies Between Phases

```
Phase 0 (Infrastructure)
    └── Phase 1 (Extended VHDL)
            └── Phase 2 (Nios II)
                    └── Phase 3 (HPS Bare Metal)
                            └── Phase 4 (Interrupts)
                                    └── Phase 5 (Debugging)
                                            └── Phase 6 (Embedded Linux)
                                                    └── Phase 7 (Ethernet)
```

Each phase gate-checks:
1. All simulation tests pass (`make sim` exits 0)
2. CI/CD pipeline is green
3. `doc/README.md` is complete and accurate
4. `make all` from the project directory succeeds on a clean clone
5. No step in the README requires opening a GUI tool
