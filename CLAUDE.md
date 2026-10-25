# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repo is

A tutorial series for the Terasic DE10-Nano (Intel/Altera Cyclone V SoC: FPGA + dual ARM Cortex-A9 HPS). Each numbered top-level directory is one self-contained "phase" project, ordered by increasing complexity: pure VHDL → Nios II / Nios V soft cores → HPS bare-metal → interrupts → JTAG/GDB debugging → Buildroot Linux → Ethernet/Protobuf → Zephyr → secure boot → OTA field update → OPC UA. The `b`-suffixed directories (`04b`, `06b`, `08b`) are Nios V/m (RISC-V, Quartus 25.1) counterparts of the Nios II (Quartus 23.1) projects.

The phase-to-folder-to-doc map lives in `README.md`; the docs hub is `docs/README.md`.

## Build system architecture

There is no top-level build. Every project is built from its own Makefile, and almost all of them include the shared fragment `common/make/quartus-version.mk`. Read that file before changing any Makefile. Key concepts:

- **Version resolution**: each Makefile sets `QUARTUS_SUPPORTED` / `QUARTUS_DEFAULT` before the include. If you don't set `QUARTUS_VERSION=...`, `common/scripts/detect-quartus.sh` looks for a local install at `$ALTERA_LITE_ROOT/<ver>std` (default `~/tools/altera_lite`). If it finds none, the build falls back to the Docker image `cvsoc/quartus:<ver>`.
- **`$(call QTOOL,cmd)`** runs a Quartus/Platform Designer/Nios command inside `<project>/quartus/`, either locally (PATH is prepended) or in the Quartus container (repo mounted at `/work`).
- **`$(call SWTOOL,cmd)`** runs software tools (ARM gcc/gdb, etc.) in the `cvsoc/tools:1.0` container, from `<project>/`.
- **`QROOT` vs `REPO_ROOT`**: inside QTOOL/SWTOOL command strings, use `$(QROOT)/...`. It is `/work` in Docker and the real path locally. Use `$(REPO_ROOT)/...` only for host-side things (docker `-v`, scp, openocd).
- **USB-Blaster**: every programming/download target depends on `usb-ready`, which auto-selects `usb-linux` (native) or `usb-wsl` (usbipd-win attach; `USBIPD_BUSID`, default `2-4`). `make usb-windows` hands the blaster back to Windows.

Docker images (build once):
```
docker build -t cvsoc/quartus:23.1 common/docker/
docker build -t cvsoc/tools:1.0 -f common/docker/Dockerfile.tools common/docker/
```
`.devcontainer/` uses `cvsoc/quartus:23.1` with USB passthrough and the `uname_shim.sh` override.

## Two project layouts

1. **FPGA projects** (`00`–`09`, `14`, `15`): run `make` from `<project>/quartus/`. Sources:
   - `hdl/`: VHDL top level
   - `qsys/*.tcl`: Platform Designer system as a script. The `.qsys` and `*_gen/` outputs are generated and gitignored; edit the `.tcl`, never the `.qsys`.
   - `quartus/de10_nano_project.tcl` and `de10_nano_pin_assignments.tcl`: create the project. `.qsf`/`.qpf` are generated and gitignored.
   - `software/app/`: firmware. `software/bsp/` is generated.

   Typical targets: `all`, `qsys`, `project`, `compile`, `bsp`, `app`, `program-sof`, `download-elf`, `terminal`, `check_timing`, `clean`. The revision name is always `de10_nano`.

   HPS projects (`05`, `07`, `09`, `14`, `15`) use a two-pass DDR3 compile (`quartus_map` → `hps_sdram_p0_pin_assignments.tcl` via `quartus_sta` → `map/fit/asm/sta`), and `05` patches generated OCT RTL with `scripts/patch_oct.py`. Don't collapse this into `quartus_sh --flow compile`.
2. **Linux/RTOS projects** (`10`, `11`, `12`, `13`, `16`, `17`): run `make` from the project root. These download and build Buildroot 2024.11.1 in-tree using a `br2-external/` tree. Common targets: `rbf`, `buildroot`, `sdcard`, `flash SDCARD=/dev/sdX`, `server-cross`/`app-cross`, `help`. `12_zephyr_led` uses west (`init`, `build`, `flash`, `debug`, `pristine`).

## Cross-project binary dependencies

Later phases reuse artifacts from earlier ones (full table in `docs/project_dependencies.md`):
- `10_linux_led` turns the `05_hps_led` `.sof` into `de10_nano.rbf`. `11`, `16` and `17` copy that `.rbf` with `make rbf`, which triggers `make -C ../10_linux_led rbf` if it is missing.
- `08_nios2_debug` reuses the `.sof` and BSP from `06_nios2_interrupts`. `09_hps_debug` reuses the `.sof` from `07_hps_interrupts`.
- `12_zephyr_led` needs the U-Boot SPL built by Buildroot in `10_linux_led`.
- `common/ip/power_on_reset/` is shared VHDL IP. `common/software/niosv_shell/` holds the header-only C++ Nios V register shell.

## Tests (no board required)

Host-side Python tests exist only for the networked phases:
```
make -C 11_ethernet_hps_led test            # raw UDP protocol
make -C 11_ethernet_hps_led test-pb         # nanopb/protobuf protocol
make -C 11_ethernet_hps_led test-dashboard  # web gateway
make -C 16_field_update test                # OTA daemon (daemon/test_ota_daemon.py)
make -C 17_opcua_hps_led test               # builds server in --sim mode, runs client/test_opcua.py
```
To run one test file, run its script directly (e.g. `cd 11_ethernet_hps_led/client && python3 test_protocol.py`). After editing `11_ethernet_hps_led/proto/led_command.proto`, run `make proto` to regenerate the nanopb C stubs and the Python `_pb2`. FPGA timing is checked with `make check_timing` (`00_led_blinking/scripts/check_timing_slacks.py`).

## Conventions

- EditorConfig: LF line endings, final newline, 2-space indent for VHDL/Verilog, tabs in Makefiles.
- Commits use Conventional Commits, with the project directory as the scope: `feat(17_opcua_hps_led): ...`, `fix(10_linux_led): ...`, `docs(howto): ...`, `refactor(common): ...`.
- Docs follow Diátaxis. Repo-wide docs go in `docs/`, named `tutorial_*`, `explanation_*`, `howto_*`, or as references (`command-reference.md`, `project_dependencies.md`). Per-project docs go in `<project>/doc/`. When you add a phase, update the tables in `README.md` and `docs/README.md` and the dependency table. Design plans live in `docs/plans/` and `docs/superpowers/`.
