# Tutorial — Multi-Version Quartus Support

> **Series:** cvsoc — Stepping into advanced FPGA development on the DE10-Nano  
> **Type:** Tutorial (tooling / infrastructure)  
> **Difficulty:** Intermediate — you are comfortable with the project Makefiles, TCL, and Docker

---

## What you will learn

By the end of this tutorial you will understand how the repository builds every project
with the **correct Quartus version** — and why it now needs two of them:

- Why the repo supports both **Quartus Prime 23.1** and **25.1**
- How the effective version is chosen **per project**, in what order
- The manifest variables each project Makefile declares and the shared `quartus-version.mk`
  fragment they include
- The difference between `QROOT` and `REPO_ROOT` and when to use which
- The slim **software-tools container** (`cvsoc/tools:1.0`) that replaced the fat Quartus
  image for ARM cross-compilation
- How to add support for a **new Quartus version**
- When to use **Nios II** (23.1) vs **Nios V** (25.1)
- The common build and program commands

---

## Prerequisites

| Requirement | Details |
|---|---|
| **Repository** | `git clone` of `bleviet/cvsoc` on the current branch |
| **Docker** | `cvsoc/quartus:23.1` and `cvsoc/tools:1.0` images available |
| **Quartus 25.1** | Local install under `$HOME/tools/altera_lite/25.1std` (optional but recommended) |
| **WSL2** | Ubuntu on WSL2 with Docker Desktop integration (for JTAG programming) |

---

## Background: why two Quartus versions?

The original series was built entirely on **Quartus Prime Lite 23.1**, running inside the
`cvsoc/quartus:23.1` Docker container. Everything — synthesis, Nios II EDS, even the ARM
cross-compiler — lived in that one image.

Then Quartus Prime Lite **25.1** arrived with a significant new feature for this series:
the **Nios V/m**, a RISC-V soft-core processor. The `intel_niosv_m` IP **does not exist in
23.1**, and the Nios V tooling (`niosv-bsp`, `niosv-app`, `niosv-download`) ships only with
25.1. To build Nios V designs we therefore need 25.1 — but the existing Nios II, HPS, and
pure-HDL projects are all perfectly happy on 23.1.

So instead of forcing one version on every project, the repository now:

1. Runs **23.1 only via Docker** (`cvsoc/quartus:23.1`) — it is never installed locally.
2. Runs **25.1 only from a local install** at `$HOME/tools/altera_lite/25.1std`.
3. Runs the **software toolchain** (ARM cross-gcc, GDB, `scp`, OpenOCD) in a new, slim
   `cvsoc/tools:1.0` container that is **independent of the Quartus version**.

A single shared make fragment, `common/make/quartus-version.mk`, resolves the effective
version per project and exports the runner macros every project Makefile uses.

---

## How version selection works

The effective version is chosen with **three levels of precedence**:

```
QUARTUS_VERSION override  →  local install probe  →  Docker default
```

| Priority | Mechanism | Condition |
|---|---|---|
| 1 | `QUARTUS_VERSION` (env var or `make` argument) | Must be in the project's `QUARTUS_SUPPORTED` list; otherwise `make` aborts with `does not support Quartus <ver>`. |
| 2 | Local probe — `common/scripts/detect-quartus.sh` | For each version in `QUARTUS_SUPPORTED` (in order), the script checks whether `$ALTERA_LITE_ROOT/<ver>std/quartus/bin/quartus_sh` is executable. The first hit wins. |
| 3 | Docker default — `QUARTUS_DEFAULT` | If no local install is found, use this version in the Docker container. |

### The local probe

`common/scripts/detect-quartus.sh` is a tiny, dependency-free script:

```bash
# Check, in order, for a local install of 25.1, then 23.1
ALTERA_LITE_ROOT="${ALTERA_LITE_ROOT:-$HOME/tools/altera_lite}"

for ver in "$@"; do
    root="$ALTERA_LITE_ROOT/${ver}std"
    if [[ -x "$root/quartus/bin/quartus_sh" ]]; then
        printf '%s\n' "$ver"
        exit 0
    fi
done

exit 1
```

`ALTERA_LITE_ROOT` defaults to `$HOME/tools/altera_lite` and can be overridden in the
environment — that is the single knob that relocates every local Quartus install.

### Runner selection

Once the version is known, the fragment decides **how** to run it:

```make
QUARTUS_HOME := $(ALTERA_LITE_ROOT)/$(QUARTUS_VERSION)std
ifeq ($(shell test -x "$(QUARTUS_HOME)/quartus/bin/quartus_sh" && echo yes),yes)
  QUARTUS_RUNNER := local      # use the local install directly
else
  QUARTUS_RUNNER := docker     # docker run cvsoc/quartus:$(QUARTUS_VERSION)
endif
```

So on a machine with 25.1 installed, `make all` auto-detects 25.1 and runs it locally. On
a machine with no local Quartus at all, it falls back to Docker 23.1. No environment
variables need to be set in either case.

---

## The manifest variables and the include pattern

Every project Makefile declares a small set of manifest variables **before** including the
shared fragment. The fragment is included with a **relative path**, so no absolute
`REPO_ROOT` is needed in the include line.

```make
# quartus/Makefile of a project in its own directory (00, 01, 04, 04b, 05, ...)
include ../../common/make/quartus-version.mk

# Makefile at a project's root (10, 11, 12, 13 — no quartus/ subdirectory)
include ../common/make/quartus-version.mk
```

### Input variables

| Variable | Default | Meaning |
|---|---|---|
| `PROJECT_NAME` | — | Directory/project name, used to build tool paths (`$(QROOT)/$(PROJECT_NAME)/...`). |
| `REVISION_NAME` | — | Quartus revision, used for the `.sof` filename. |
| `QUARTUS_SUPPORTED` | `23.1` | Space-separated versions this project works with. |
| `QUARTUS_DEFAULT` | `23.1` | Version used when no local install is detected (Docker fallback). |
| `NEEDS_SOFTWARE_TOOLS` | `0` | Set to `1` when the project's ARM app is built in `cvsoc/tools:1.0`. |

### Exported variables and macros

| Export | Meaning |
|---|---|
| `QUARTUS_VERSION` | The effective version after resolution. |
| `QUARTUS_HOME` | Local install root (`$ALTERA_LITE_ROOT/<ver>std`), or empty in Docker mode. |
| `QUARTUS_BIN` | Directory containing `quartus_sh`, `quartus_map`, etc. (empty in Docker mode). |
| `QROOT` | Repo root **as seen by the executing context** — see below. |
| `QTOOL` | `$(call QTOOL,cmd)` — run a Quartus tool in the project's `quartus/` dir. |
| `SWTOOL` | `$(call SWTOOL,cmd)` — run a software tool in the `cvsoc/tools:1.0` container. |
| `QSYS_TOOL` / `NIOS_TOOL` | Named tool groups; the right binary is resolved via `QTOOL`'s PATH per version. |
| `QUARTUS_PROGRAM` | JTAG-programs the `.sof` (context-aware; used by `program-sof`). |
| `usb-wsl` / `usb-windows` | USB-Blaster attach/detach targets (WSL2 + `usbipd.exe`). |
| `DOCKER_IMAGE` | `cvsoc/quartus:$(QUARTUS_VERSION)` in Docker mode. |

The fragment also exports `ALTERA_LITE_ROOT`, `TOOLS_IMAGE` (`cvsoc/tools:1.0`), and the
`USBIPD` / `USBIPD_BUSID` / `DEVICE_INDEX` programming defaults.

### A complete example

From `00_led_blinking/quartus/Makefile`:

```make
PROJECT_NAME  = 00_led_blinking
REVISION_NAME = de10_nano

QUARTUS_SUPPORTED ?= 23.1 25.1
QUARTUS_DEFAULT   ?= 23.1
NEEDS_SOFTWARE_TOOLS ?= 0

include ../../common/make/quartus-version.mk

all:
	$(call QTOOL, \
	  quartus_sh -t de10_nano_project.tcl && \
	  quartus_sh --flow compile $(PROJECT_NAME) -c $(REVISION_NAME) && \
	  python3 $(QROOT)/00_led_blinking/scripts/check_timing_slacks.py \
	    $(REVISION_NAME).sta.rpt 2>/dev/null || true)
```

Note how the recipe uses `$(QROOT)` — not `$(REPO_ROOT)` — for the Python script path.

---

## QROOT vs REPO_ROOT

This is the one subtlety that trips people up. The fragment defines **two** "repo root"
variables because the tools run in **two different contexts**:

- **`REPO_ROOT`** is the repo root **on the host** (where `make` runs). It is used only for
  host-side references: `docker run -v` mounts, `scp`, the `uname` shim path.
- **`QROOT`** is the repo root **as the executing tool sees it**. In local mode the tools
  run on the host, so `QROOT = $(REPO_ROOT)`. In Docker mode the tools run inside the
  container with the repo mounted at `/work`, so `QROOT = /work`.

```
Mode    REPO_ROOT (host)          QROOT (as tools see it)
local   /home/user/cvsoc          /home/user/cvsoc
docker  /home/user/cvsoc          /work
```

**Rule of thumb:** inside a `$(call QTOOL, ...)` or `$(call SWTOOL, ...)` command string,
always use `$(QROOT)/...`. Use `$(REPO_ROOT)/...` only in host-side commands (`docker -v`
mounts, `scp`, `openocd` invoked on the host).

---

## The software-tools container (`cvsoc/tools:1.0`)

Historically the ARM cross-compiler, GDB, and OpenSSH were baked into the fat Quartus image
`cvsoc/quartus:23.1`. That bundled a 12 GB image with software tools that have **nothing to
do with Quartus** — and it tied the ARM toolchain to the 23.1 container even when the FPGA
was synthesized with 25.1.

The software tools now live in a slim, version-independent container:

| Tool | Purpose |
|---|---|
| `arm-linux-gnueabihf-gcc` / `-objcopy` / `-size` | ARM cross-compiler and binutils (HPS bare-metal and Linux apps) |
| `gdb-multiarch` (symlinked as `arm-none-eabi-gdb`) | ARM bare-metal debugger |
| `openssh-client` (`scp`) | Deploying ELF/binary to the HPS over Ethernet |
| `openocd` | JTAG debug server for HPS debugging |
| `python3`, `make` | Build helpers and patch scripts |

It is built from `common/docker/Dockerfile.tools` on **Ubuntu 22.04** (matching the WSL2
host glibc):

```bash
docker build -t cvsoc/tools:1.0 -f common/docker/Dockerfile.tools common/docker/
```

Projects that need it set `NEEDS_SOFTWARE_TOOLS ?= 1` and invoke it through `SWTOOL`:

```make
app:
	$(call SWTOOL,make -C software/app CC=$(ARM_CC))
```

`SWTOOL` mounts the repo at `/work`, runs as your host user, and executes the command from
the project's root directory — same semantics as `QTOOL`, but always in `cvsoc/tools:1.0`
regardless of the Quartus version.

---

## Adding a new Quartus version

Because the fragment is data-driven, supporting a new version is mostly a matter of
**installing it where the probe expects it** and **declaring it supported**.

1. **Install Quartus** so that `quartus_sh` is executable at
   `$ALTERA_LITE_ROOT/<ver>std/quartus/bin/quartus_sh`. The `ALTERA_LITE_ROOT` environment
   variable relocates the whole tree if you do not use the default
   `$HOME/tools/altera_lite`.

   ```bash
   # Example: add Quartus 26.0
   # Install to $HOME/tools/altera_lite/26.0std, then verify the probe sees it:
   common/scripts/detect-quartus.sh 26.0 25.1 23.1
   # Expected: 26.0
   ```

2. **Declare it supported** in each project that can build with it by adding the version to
   `QUARTUS_SUPPORTED`:

   ```make
   QUARTUS_SUPPORTED ?= 23.1 25.1 26.0
   ```

   The probe checks versions **in the order they appear** in `QUARTUS_SUPPORTED`, so list the
   preferred local version first.

3. **Set `QUARTUS_DEFAULT`** if you want it as the Docker fallback (you would also need a
   `cvsoc/quartus:<ver>` image for that path). Otherwise leave the default as is.

4. If the new version needs its own **Nios tool group** (like `niosv-bsp` vs
   `nios2-bsp`), add a branch in the fragment's `NIOS_TOOL` section.

No other changes are required — `make all` picks the new version up automatically.

---

## Nios II vs Nios V guidance

| Family | Projects | Core | Quartus | Notes |
|---|---|---|---|---|
| **Nios II** (classic) | `04_nios2_led`, `06_nios2_interrupts`, `08_nios2_debug` | Nios II/e | **23.1 only** | Built entirely in Docker 23.1. The `nios2-*` EDS tools ship in that image. |
| **Nios V** (RISC-V) | `04b_niosv_led`, `06b_niosv_interrupts`, `08b_niosv_debug` | Nios V/m | **25.1 only** | `intel_niosv_m` does not exist in 23.1. Built with local 25.1. |

The two families are separate **sibling projects** (`04b` is the sibling of `04`, and so on)
because they cannot share a single Platform Designer system — the CPU IP differs.

> **Nios V software toolchain (pending):** Quartus 25.1 ships only the `niosv-bsp` /
> `niosv-app` / `niosv-download` shells. The actual compiler is the **RiscFree RISC-V
> toolchain**, which is not bundled. Until it is installed, the Nios V **hardware** targets
> (`make qsys project compile QUARTUS_VERSION=25.1`) build fine, but the **software**
> targets (`make bsp app`) fail with a toolchain error. The `*-b` READMEs document the
> exact behavior per project.

Rules enforced by the fragment:

- `make QUARTUS_VERSION=25.1` in a Nios II project aborts with
  `04_nios2_led does not support Quartus 25.1 (supported: 23.1)`.
- `make QUARTUS_VERSION=23.1` in a Nios V project aborts with
  `04b_niosv_led does not support Quartus 23.1 (supported: 25.1)`.

---

## Common commands

All builds are driven from the project's `quartus/` directory (or the project root for
`10`–`13`), and **auto-detect the version**:

```bash
cd 00_led_blinking/quartus
make all                 # auto-detect: local 25.1 if installed, else Docker 23.1
```

To force a version:

```bash
make QUARTUS_VERSION=25.1 all    # compile with local 25.1
make QUARTUS_VERSION=23.1 all    # compile inside cvsoc/quartus:23.1
```

Per-step hardware targets (`qsys`, `project`, `compile`, `check_timing`), software targets
(`bsp`, `app`), and Nios-specific targets (`download-elf`, `terminal`) all take the same
version override.

Programming the board (WSL2):

```bash
cd 00_led_blinking/quartus
make usb-wsl USBIPD_BUSID=2-4          # attach USB-Blaster to WSL2 (replace 2-4)
make program-sof USBIPD_BUSID=2-4      # JTAG-program the .sof
make usb-windows USBIPD_BUSID=2-4      # detach back to Windows when done
```

Summary of the project matrix:

| Projects | Type | `QUARTUS_SUPPORTED` | Runner |
|---|---|---|---|
| `00_led_blinking`, `01_led_running` | Pure HDL | `23.1 25.1` | local 25.1 → Docker 23.1 |
| `04/06/08` | Nios II | `23.1` | Docker 23.1 |
| `04b/06b/08b` | Nios V | `25.1` | local 25.1 |
| `05/07/09/14/15` | HPS (+ ARM app) | `23.1 25.1` | local 25.1 → Docker 23.1, app via `cvsoc/tools:1.0` |
| `10/11/12/13` | Root-level (Linux/Zephyr/secure boot) | `23.1 25.1` | local 25.1 → Docker 23.1 |

---

## Troubleshooting

### `make: *** <project> does not support Quartus <ver>.  Stop.`

The forced `QUARTUS_VERSION` is not in the project's `QUARTUS_SUPPORTED` list — for
example forcing 25.1 in a Nios II project. Either drop the override and let auto-detect
run, or pick a supported version.

### `make all` picked a different version than I expected

The probe returns the **first** version in `QUARTUS_SUPPORTED` that has an executable
`quartus_sh`. Check what is installed:

```bash
common/scripts/detect-quartus.sh 23.1 25.1     # prints 25.1 if present
common/scripts/detect-quartus.sh 23.1          # prints nothing, exit 1 → Docker path
```

If you want to see what the fragment chose without building, print it from any project:

```bash
make -p 2>/dev/null | grep -E '^(QUARTUS_VERSION|QUARTUS_RUNNER|QUARTUS_HOME) ?=' | head
```

### Quartus install is somewhere other than `$HOME/tools/altera_lite`

Set `ALTERA_LITE_ROOT` in your environment (or pass it to `make`). The probe, `QUARTUS_HOME`,
and the local runner all honor it:

```bash
export ALTERA_LITE_ROOT=/opt/intelFPGA_lite
make all
```

### `docker: Failed to create task for container ... cvsoc/quartus:23.1: Not Found`

The Docker fallback image is not pulled. Build it first:

```bash
docker build -t cvsoc/quartus:23.1 common/docker/
```

### Nios V `make bsp app` fails with a toolchain error

The RiscFree RISC-V toolchain is not installed (it is not bundled with Quartus 25.1). The
FPGA hardware builds regardless; install the toolchain to enable the software targets.

---

## Summary

| Concept | Where it lives |
|---|---|
| Version resolution (override → probe → default) | `common/make/quartus-version.mk` |
| Local install probe | `common/scripts/detect-quartus.sh` |
| Software cross-toolchain (no Quartus) | `common/docker/Dockerfile.tools` → `cvsoc/tools:1.0` |
| Version selection per project | `QUARTUS_SUPPORTED` / `QUARTUS_DEFAULT` in each Makefile |
| Context-aware repo root | `QROOT` inside `QTOOL`/`SWTOOL`, `REPO_ROOT` on the host |

The same `make all` works on a machine with only Docker, only local 25.1, or both — the
fragment picks the right Quartus for each project automatically.
