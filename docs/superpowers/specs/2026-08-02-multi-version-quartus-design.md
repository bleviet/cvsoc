# Multi-Version Quartus Support — Design

**Date:** 2026-08-02
**Status:** Approved for implementation

## Problem

Every project in this repository hardcodes Quartus Prime 23.1 running inside the
`cvsoc/quartus:23.1` Docker container (e.g. `00_led_blinking/quartus/Makefile:27`).
This couples the repository to a single Quartus version. Users who have a different
Quartus version installed locally — for example Quartus 25.1 Lite at
`$HOME/tools/altera_lite/25.1std` — cannot build the projects with their install.

Compatibility is not uniform across projects:

- Quartus 25.1 **removed Nios II classic BSP tooling** (`nios2-bsp` is absent; only
  `niosv-bsp` ships). Projects 04/06/08 use Nios II classic and cannot build under 25.1.
- Quartus 25.1 **does support the DE10-Nano device** `5CSEBA6U23` (verified in its
  device database) and ships `niosv-bsp` for Nios V.
- HPS projects (05/07/09/10/11/14/15) additionally need ARM cross-toolchains
  (`arm-linux-gnueabihf-{gcc,objcopy,size}`), `arm-none-eabi-gdb`, `scp`/`ssh`, and
  `openocd`, which currently only exist inside the 23.1 Docker image.

## Goal

Make the repository support multiple Quartus versions, easy to extend to future ones:

1. All tracked projects (14 active) build with the version that makes sense for them.
2. Per-project version support is declared, not inferred from failures.
3. Adding support for a new Quartus version is a small, well-defined change.
4. Keep 23.1 (Docker) working as the fallback for everything.
5. Nios II classic projects stay 23.1-only; Nios V ports ship as new sibling projects.

## Non-goals

- Do not port Nios II projects in place to Nios V. New sibling projects are used.
- Do not install ARM cross-toolchains on the WSL2 host. A slim Docker image provides them.
- Do not drop the Docker-based 23.1 workflow; it remains the default fallback.

## Architecture

### New components

```
common/
├── make/
│   └── quartus-version.mk        # shared fragment: version detection + runner API
├── docker/
│   ├── Dockerfile.tools          # slim image → cvsoc/tools:1.0 (software tools only)
│   └── (uname_shim.sh reused as-is for Nios II in 23.1 Docker)
└── scripts/
    └── detect-quartus.sh         # returns first available local Quartus version
```

### Two execution contexts

The fragment normalizes two distinct tool groups:

1. **Per-version Quartus** (`QTOOL` runner) — `quartus_sh/map/fit/asm/pgm`,
   `qsys-script`/`qsys-generate`, and Nios II / Nios V EDS tools. The selected Quartus
   version determines whether the tool runs from a local install or a Docker image.
2. **Software tools** (`SWTOOL` runner) — `arm-linux-gnueabihf-{gcc,objcopy,size}`,
   `arm-none-eabi-gdb`, `scp`/`ssh`, `openocd`. Always run in the slim
   `cvsoc/tools:1.0` container. These are version-independent of Quartus.

### Version → runner mapping (v1)

| Version | Runner | Why |
|---|---|---|
| 23.1 | Docker `cvsoc/quartus:23.1` | Existing image; Nios II classic only exists here |
| 25.1 | Local `$HOME/tools/altera_lite/25.1std` | Natively installed; Nios V available |
| future N | Local probe, else Docker if an image exists | Fragment probe + optional image |

## Fragment API

### Project manifest

Each project `quartus/Makefile` declares its toolchain needs, then includes the fragment:

```make
QUARTUS_SUPPORTED ?= 23.1          # space-separated versions this project works with
QUARTUS_DEFAULT   ?= 23.1          # fallback when nothing is detectable (docker)
NEEDS_SOFTWARE_TOOLS ?= 0          # 1 for HPS projects needing ARM gcc/gdb/scp/openocd

include $(REPO_ROOT)/common/make/quartus-version.mk
```

### Detection order

1. `QUARTUS_VERSION` (env or make var) — if set, must be in `QUARTUS_SUPPORTED`, else error.
2. `common/scripts/detect-quartus.sh $(QUARTUS_SUPPORTED)` — scans **local** install roots
   (`$HOME/tools/altera_lite/<ver>std`, plus overridable `ALTERA_LITE_ROOT`) for
   `quartus/bin/quartus_sh`; returns the first match. Docker-only versions (23.1 here)
   are never returned by the local probe.
3. Fallback — Docker runner for `QUARTUS_DEFAULT` (23.1).

For a project supporting `23.1 25.1` with 25.1 installed locally, detection resolves to
local 25.1. If only 23.1 Docker is present, the local probe finds nothing and the
fragment falls back to the Docker 23.1 runner.

### Exported variables and commands

| Name | Meaning |
|---|---|
| `QUARTUS_VERSION` | effective version |
| `QUARTUS_HOME` | local install root, or empty when using Docker |
| `QTOOL` | prefix running any Quartus tool in the correct context |
| `QSYS_TOOL` | wrapper for `qsys-script` / `qsys-generate` (Nios projects) |
| `NIOS_TOOL` | Nios EDS tools (`nios2-bsp`, `niosv-bsp`, ...) per version |
| `SWTOOL` | prefix running software tools in the slim tools container |
| `QUARTUS_PROGRAM` | target programming the `.sof` via `quartus_pgm` (handles jtagd + `/dev/bus/usb`) |

### Key behaviors

- Nios II projects (04/06/08) declare `QUARTUS_SUPPORTED=23.1` → auto-select always picks
  Docker; selecting 25.1 produces a clear error instead of `nios2-bsp: not found`.
- `make QUARTUS_VERSION=25.1 ...` overrides detection and errors if the project cannot
  use that version.
- `usb-wsl` / `usbipd` attach logic moves into the fragment's programming/download-elf
  helpers so projects stop duplicating it.

## Per-project migration

### Migrated Makefile shape

Makefiles live in `quartus/Makefile` for most projects, but 10/11/12/13 use a root
`Makefile`. All locations include the same fragment.

```make
PROJECT_NAME        = 00_led_blinking
REVISION_NAME       = de10_nano
QUARTUS_SUPPORTED   ?= 23.1 25.1
NEEDS_SOFTWARE_TOOLS ?= 0

include $(REPO_ROOT)/common/make/quartus-version.mk

all:
	$(QTOOL) "quartus_sh -t de10_nano_project.tcl && \
	          quartus_sh --flow compile $(PROJECT_NAME) -c $(REVISION_NAME)"

program-sof: $(REVISION_NAME).sof
	$(QUARTUS_PROGRAM)
```

### Per-project manifest values

| Project | Makefile location | QUARTUS_SUPPORTED | needs SW tools |
|---|---|---|---|
| 00_led_blinking, 01_led_running | `quartus/Makefile` | `23.1 25.1` | no |
| 04/06/08 (Nios II) | `quartus/Makefile` | `23.1` | no (Nios II EDS is per-version) |
| 05/07/09 (HPS) | `quartus/Makefile` | `23.1 25.1` | yes (ARM gcc/gdb/ssh) |
| 10_linux_led, 11_ethernet_hps_led | `Makefile` | `23.1 25.1` | yes (ARM gcc + scp) |
| 12_zephyr | `Makefile` | `23.1 25.1` | own toolchain, not the SW container |
| 13_secure_boot | `Makefile` | `23.1 25.1` | openssl local + `quartus_cpf` |
| 14/15 (DDR HPS) | `quartus/Makefile` | `23.1 25.1` | yes (ARM gcc + scp) |

`16_ipcraft_led_avmm` contains only untracked build artifacts and is not part of the
migration.

### Nios V sibling projects (new)

- `04b_niosv_led`, `06b_niosv_interrupts`, `08b_niosv_debug` — mirror 04/06/08, use
  `qsys/niosv_system.tcl` + `niosv-bsp`, declare `QUARTUS_SUPPORTED=25.1`.
- Nios II projects stay untouched (23.1-only).

### Error handling

Unsupported version → fragment prints e.g.
`04_nios2_led does not support Quartus 25.1 (needs Nios II classic, available only in 23.1)`
and exits 1 before doing any work.

## Software tools container

`common/docker/Dockerfile.tools` builds `cvsoc/tools:1.0`:

- Base: `ubuntu:22.04` (matches the WSL2 host glibc; avoids the frozen Debian 9 snapshot).
- Packages: `gcc-arm-linux-gnueabihf`, `binutils-arm-linux-gnueabihf`,
  `libc6-dev-armhf-cross`, `linux-libc-dev-armhf-cross`, `gdb-arm-none-eabi`,
  `openssh-client`, `python3`, `make`.
- Smoke-test `RUN` verifying each binary on PATH (same pattern as the existing Dockerfile).
- No Quartus — purely the software cross-toolchain.

## Verification strategy

1. Build `cvsoc/tools:1.0`; smoke-test ARM gcc/gdb.
2. 00/01 compile **and program** with local 25.1 (proves native runner + USB).
3. 04 Nios II full build with Docker 23.1 (proves docker `QTOOL` + uname shim).
4. 05 HPS build with 25.1 local + tools container (proves the split).
5. Negative test: `make QUARTUS_VERSION=25.1` on 04 → clear error.
6. Auto-detect test: unset `QUARTUS_VERSION`, confirm the fragment picks local 25.1.

## Documentation

- New tutorial in `docs/` covering multi-version support.
- Refresh `docs/tutorial_docker_dev_environment.md`.
- Update project READMEs' build/program commands.

## Acceptance criteria

- `make all` in 00/01 compiles with local 25.1 and programs the board via JTAG.
- `make all` in 04 (Nios II) builds with Docker 23.1 unchanged.
- `make QUARTUS_VERSION=25.1 all` in 04 fails with the documented clear error.
- `make all` in 05 (HPS) builds with local 25.1 + `cvsoc/tools:1.0`.
- Unset `QUARTUS_VERSION` on a machine with only 23.1 Docker still builds everything.
- Adding a hypothetical Quartus 26.1 requires only a fragment probe block change.
