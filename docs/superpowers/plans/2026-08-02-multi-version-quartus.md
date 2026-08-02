# Multi-Version Quartus Support — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the cvsoc repository build every project with the correct Quartus version — local 25.1 when available, Docker 23.1 as fallback — via a shared version-detection fragment.

**Architecture:** A shared make fragment (`common/make/quartus-version.mk`) resolves the effective Quartus version per project (auto-detect local installs → Docker 23.1 fallback), and exports runner macros (`QTOOL`, `SWTOOL`, `QUARTUS_PROGRAM`) that all project Makefiles use. Software-only toolchains (ARM cross-gcc/gdb/scp/openocd) move to a new slim `cvsoc/tools:1.0` image. Nios II projects stay 23.1-only; Nios V siblings are new projects under 25.1.

**Tech Stack:** GNU Make, Bash, Docker, Quartus Prime Lite 23.1/25.1, Tcl, VHDL/Verilog.

## Global Constraints

- Quartus 23.1 runs **only** via Docker image `cvsoc/quartus:23.1`. Never installed locally.
- Quartus 25.1 runs **only** from local install at `$HOME/tools/altera_lite/25.1std` (overridable via `ALTERA_LITE_ROOT`).
- Software tools (ARM cross-gcc/objcopy/size, gdb-arm-none-eabi, openssh-client, openocd) run **only** in `cvsoc/tools:1.0`.
- Nios II classic projects (04/06/08) declare `QUARTUS_SUPPORTED=23.1` only.
- All other projects declare `QUARTUS_SUPPORTED=23.1 25.1`.
- `QUARTUS_VERSION` env/make-var always wins and must be in `QUARTUS_SUPPORTED`.
- Auto-detect order: `QUARTUS_VERSION` → local probe → Docker 23.1 default.
- No comments in generated code beyond what matches the existing file style.
- All verification must be run from the repo root in WSL2, with the USB-Blaster attached (busid varies per machine).

---

### Task 1: Detect script — `common/scripts/detect-quartus.sh`

**Files:**
- Create: `common/scripts/detect-quartus.sh`
- Test: run manually against the real 25.1 install

**Interfaces:**
- Produces: exit 0 + prints `<version>` for the first locally-installed version in its argument list; exit 1 + prints nothing if none found.

- [ ] **Step 1: Write the detect script**

```bash
#!/usr/bin/env bash
# detect-quartus.sh — print the first locally-installed Quartus version.
#
# Usage: detect-quartus.sh VERSION [VERSION ...]
#   Checks, for each VERSION in order, whether a local install exists at
#   $ALTERA_LITE_ROOT/<VERSION>std/quartus/bin/quartus_sh.
#   Prints the first VERSION found and exits 0; exits 1 if none found.
#
# ALTERA_LITE_ROOT defaults to $HOME/tools/altera_lite.
set -euo pipefail

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

- [ ] **Step 2: Make it executable and verify**

Run:
```bash
chmod +x common/scripts/detect-quartus.sh
common/scripts/detect-quartus.sh 23.1 25.1
common/scripts/detect-quartus.sh 23.1
echo "exit=$?"
```

Expected:
- First command prints `25.1` (since 25.1 is installed locally, 23.1 is not).
- Second command prints nothing, `exit=1`.

- [ ] **Step 3: Verify ALTERA_LITE_ROOT override**

Run:
```bash
ALTERA_LITE_ROOT=/nonexistent common/scripts/detect-quartus.sh 25.1
echo "exit=$?"
```
Expected: no output, `exit=1`.

- [ ] **Step 4: Commit**

```bash
git add common/scripts/detect-quartus.sh
git commit -m "feat(common): add detect-quartus.sh local version probe"
```

---

### Task 2: Software tools container — `common/docker/Dockerfile.tools`

**Files:**
- Create: `common/docker/Dockerfile.tools`
- Test: `docker build` + smoke-test

**Interfaces:**
- Produces: image `cvsoc/tools:1.0` with `arm-linux-gnueabihf-{gcc,objcopy,size}`, `arm-none-eabi-gdb`, `scp`/`ssh`, `openocd`, `python3`, `make`.

- [ ] **Step 1: Write the Dockerfile**

```dockerfile
# cvsoc/tools:1.0 — Software cross-toolchain container (no Quartus).
#
# Provides the ARM cross-compilers, debugger, and network tools required by
# the HPS phases (05/07/09/10/11/14/15) independent of the Quartus version
# used for synthesis. Ubuntu 22.04 matches the WSL2 host glibc.
#
# Build:
#   docker build -t cvsoc/tools:1.0 -f common/docker/Dockerfile.tools common/docker/

FROM ubuntu:22.04

RUN apt-get update -qq && apt-get install -y --no-install-recommends \
        gcc-arm-linux-gnueabihf \
        binutils-arm-linux-gnueabihf \
        libc6-dev-armhf-cross \
        linux-libc-dev-armhf-cross \
        gdb-arm-none-eabi \
        openssh-client \
        openocd \
        python3 \
        make \
    && rm -rf /var/lib/apt/lists/*

# Smoke-test: verify every binary is on PATH
RUN arm-linux-gnueabihf-gcc  --version | head -1 && \
    arm-linux-gnueabihf-size --version | head -1 && \
    arm-none-eabi-gdb         --version | head -1 && \
    scp --version 2>&1 | head -1 && \
    openocd --version 2>&1 | head -1

LABEL \
    org.opencontainers.image.title="cvsoc/tools" \
    org.opencontainers.image.description="Software cross-toolchains for bleviet/cvsoc HPS phases"
```

- [ ] **Step 2: Build the image**

Run:
```bash
docker build -t cvsoc/tools:1.0 -f common/docker/Dockerfile.tools common/docker/
```
Expected: build succeeds and the smoke-test `RUN` prints versions for gcc, size, gdb, scp, openocd.

- [ ] **Step 3: Commit**

```bash
git add common/docker/Dockerfile.tools
git commit -m "feat(common): add cvsoc/tools:1.0 software toolchain Dockerfile"
```

---

### Task 3: Shared fragment — `common/make/quartus-version.mk`

**Files:**
- Create: `common/make/quartus-version.mk`
- Test: `make -f` on a scratch Makefile

**Interfaces:**
- Consumes: project-defined `PROJECT_NAME`, `REVISION_NAME`, `QUARTUS_SUPPORTED`, `QUARTUS_DEFAULT`, `NEEDS_SOFTWARE_TOOLS`, and `REPO_ROOT` (fallback computed here).
- Produces:
  - `QUARTUS_VERSION` — effective version string
  - `QUARTUS_HOME` — local install root, or empty in Docker mode
  - `QUARTUS_BIN` — absolute path to the `bin` dir containing `quartus_sh`
  - `QTOOL` — make call `$(call QTOOL,cmd)` that runs `cmd` from the project's `quartus` dir in the correct context
  - `SWTOOL` — make call `$(call SWTOOL,cmd)` that runs `cmd` in the tools container
  - `QUARTUS_PROGRAM` — recipe that programs the `.sof` via JTAG (context-aware)
  - `usb-wsl` / `usb-windows` / `usbipd` variables

- [ ] **Step 1: Write the fragment**

```make
# common/make/quartus-version.mk — Multi-version Quartus runner shared by all
# project Makefiles.
#
# Each project Makefile sets these BEFORE including this fragment:
#   PROJECT_NAME, REVISION_NAME
#   QUARTUS_SUPPORTED ?= 23.1 25.1   # versions this project works with
#   QUARTUS_DEFAULT   ?= 23.1        # Docker fallback version
#   NEEDS_SOFTWARE_TOOLS ?= 0        # 1 if HPS projects need ARM gcc/gdb/scp
#
# Include with a relative path so REPO_ROOT is not needed in the include line:
#   quartus/Makefile:  include ../../common/make/quartus-version.mk
#   root Makefile:     include ../common/make/quartus-version.mk
#
# Exported after include:
#   QUARTUS_VERSION    effective version (auto-detected or QUARTUS_VERSION)
#   QUARTUS_HOME       local install root (empty when using Docker)
#   QUARTUS_BIN        dir containing quartus_sh / quartus_map etc.
#   QSYS_TOOL          qsys-script/qsys-generate wrapper (Platform Designer)
#   NIOS_TOOL          Nios EDS wrapper (nios2-bsp / niosv-bsp per version)
#   QTOOL              $(call QTOOL,cmd)  run a Quartus tool
#   SWTOOL             $(call SWTOOL,cmd) run a software tool in the tools container
#   QUARTUS_PROGRAM    JTAG-program the .sof (recipe)
#   usb-wsl, usb-windows  USB-Blaster attach/detach targets

REPO_ROOT ?= $(realpath $(dir $(lastword $(MAKEFILE_LIST)))/../..)

QUARTUS_SUPPORTED ?= 23.1
QUARTUS_DEFAULT   ?= 23.1
NEEDS_SOFTWARE_TOOLS ?= 0

ALTERA_LITE_ROOT ?= $(HOME)/tools/altera_lite
TOOLS_IMAGE ?= cvsoc/tools:1.0

USBIPD       ?= usbipd.exe
USBIPD_BUSID ?= 2-4
DEVICE_INDEX ?= 2

# ── Version resolution ────────────────────────────────────────────────────────
ifneq ($(strip $(QUARTUS_VERSION)),)
  # Explicit override: must be supported.
  ifeq ($(filter $(QUARTUS_VERSION),$(QUARTUS_SUPPORTED)),)
    $(error $(PROJECT_NAME) does not support Quartus $(QUARTUS_VERSION) (supported: $(QUARTUS_SUPPORTED)))
  endif
else
  # Auto-detect: local install first, then Docker default.
  DETECTED := $(shell $(REPO_ROOT)/common/scripts/detect-quartus.sh $(QUARTUS_SUPPORTED))
  ifneq ($(strip $(DETECTED)),)
    QUARTUS_VERSION := $(DETECTED)
  else
    QUARTUS_VERSION := $(QUARTUS_DEFAULT)
  endif
endif

# ── Runner selection ──────────────────────────────────────────────────────────
QUARTUS_HOME := $(ALTERA_LITE_ROOT)/$(QUARTUS_VERSION)std
ifeq ($(shell test -x "$(QUARTUS_HOME)/quartus/bin/quartus_sh" && echo yes),yes)
  QUARTUS_BIN   := $(QUARTUS_HOME)/quartus/bin
  QUARTUS_RUNNER := local
else
  QUARTUS_HOME  :=
  QUARTUS_BIN   :=
  QUARTUS_RUNNER := docker
endif

# ── QTOOL: run a Quartus toolchain command in the project's quartus dir ──────
ifeq ($(QUARTUS_RUNNER),local)
  # Local: prepend the install's bin dirs to PATH and run directly.
  QTOOL = cd $(REPO_ROOT)/$(PROJECT_NAME)/quartus && \
          PATH="$(QUARTUS_BIN):$(QUARTUS_HOME)/quartus/sopc_builder/bin:$(QUARTUS_HOME)/nios2eds/bin:$(QUARTUS_HOME)/niosv/bin:$$PATH" $(1)
else
  # Docker: run inside the versioned Quartus container, workspace at /work.
  DOCKER_IMAGE ?= cvsoc/quartus:$(QUARTUS_VERSION)
  QTOOL = docker run --rm --user $$(id -u):$$(id -g) \
          -v $(REPO_ROOT):/work \
          $(DOCKER_IMAGE) \
          bash -c 'cd /work/$(PROJECT_NAME)/quartus && $(1)'
endif

# ── QSYS_TOOL / NIOS_TOOL: named tool groups (resolved via PATH by QTOOL) ─────
# These identify which toolchain family a project step belongs to; the actual
# binary is invoked through QTOOL so the correct version/context is used.
QSYS_TOOL = qsys-script qsys-generate
ifeq ($(QUARTUS_VERSION),23.1)
  NIOS_TOOL = nios2-bsp nios2-bsp-create-settings nios2-elf-gcc nios2-download nios2-terminal
else
  NIOS_TOOL = niosv-bsp niosv-app niosv-download
endif

# ── SWTOOL: run a software tool in the tools container ────────────────────────
SWTOOL = docker run --rm --user $$(id -u):$$(id -g) \
         -v $(REPO_ROOT):/work \
         $(TOOLS_IMAGE) \
         bash -c 'cd /work/$(PROJECT_NAME) && $(1)'

# ── USB-Blaster attach/detach (WSL2) ─────────────────────────────────────────
usb-wsl:
	@echo "Connecting USB-Blaster (busid $(USBIPD_BUSID)) to WSL2..."
	$(USBIPD) attach --wsl --busid $(USBIPD_BUSID) 2>/dev/null || true
	@sleep 1

usb-windows:
	@echo "Detaching USB-Blaster (busid $(USBIPD_BUSID)) from WSL2 → Windows..."
	$(USBIPD) detach --busid $(USBIPD_BUSID) 2>/dev/null || true
	@sleep 1

# ── QUARTUS_PROGRAM: JTAG-program the .sof ────────────────────────────────────
ifeq ($(QUARTUS_RUNNER),local)
QUARTUS_PROGRAM = jtagd; sleep 2; \
                  quartus_pgm -m jtag -o "p;$(REPO_ROOT)/$(PROJECT_NAME)/quartus/$(REVISION_NAME).sof@$(DEVICE_INDEX)"; \
                  kill $$(pgrep jtagd) 2>/dev/null || true
else
QUARTUS_PROGRAM = docker run --rm --user $$(id -u):$$(id -g) --privileged \
                  -v /dev/bus/usb:/dev/bus/usb \
                  -v $(REPO_ROOT):/work \
                  $(DOCKER_IMAGE) \
                  bash -c 'jtagd && sleep 2 && \
                    quartus_pgm -m jtag -o "p;/work/$(PROJECT_NAME)/quartus/$(REVISION_NAME).sof@$(DEVICE_INDEX)"; \
                    kill $$(pgrep jtagd) 2>/dev/null || true'
endif
```

- [ ] **Step 2: Test version resolution with a scratch Makefile**

Create `/tmp/opencode/mk-test/Makefile`:

```make
PROJECT_NAME  = scratch
REVISION_NAME = de10_nano
QUARTUS_SUPPORTED ?= 23.1 25.1
include ../../home/balevision/workspace/bleviet/cvsoc/common/make/quartus-version.mk

print:
	@echo "VERSION=$(QUARTUS_VERSION) RUNNER=$(QUARTUS_RUNNER) HOME=$(QUARTUS_HOME)"
```

Run:
```bash
cd /tmp/opencode/mk-test && make print
make print QUARTUS_VERSION=23.1
make print QUARTUS_VERSION=99.0
```
Expected:
- First prints `VERSION=25.1 RUNNER=local HOME=$HOME/tools/altera_lite/25.1std`.
- Second prints `VERSION=23.1 RUNNER=docker HOME=` (23.1 not installed locally).
- Third fails with `scratch does not support Quartus 99.0`.

- [ ] **Step 3: Verify QTOOL context**

Append to the scratch Makefile:

```make
print-cmd:
	@echo "$(call QTOOL,echo hi)"
```
Run: `make print-cmd`
Expected (local runner): the expanded local command, containing `cd $(REPO_ROOT)/scratch/quartus && PATH="...` and `echo hi`.

- [ ] **Step 4: Commit**

```bash
git add common/make/quartus-version.mk
git commit -m "feat(common): add quartus-version.mk shared runner fragment"
```

---

### Task 4: Migrate pure-HDL projects — 00_led_blinking, 01_led_running

**Files:**
- Modify: `00_led_blinking/quartus/Makefile` (full rewrite)
- Modify: `01_led_running/quartus/Makefile` (full rewrite)

**Interfaces:**
- Consumes: fragment from Task 3 (`QTOOL`, `QUARTUS_PROGRAM`, `usb-wsl`).
- Produces: `make all` compiles with local 25.1 and programs the board.

- [ ] **Step 1: Rewrite `00_led_blinking/quartus/Makefile`**

```make
# Makefile for the 00_led_blinking Quartus project.
# Pure HDL design — no software component. Multi-version Quartus via
# common/make/quartus-version.mk (local 25.1 preferred, Docker 23.1 fallback).

PROJECT_NAME  = 00_led_blinking
REVISION_NAME = de10_nano

QUARTUS_SUPPORTED ?= 23.1 25.1
QUARTUS_DEFAULT   ?= 23.1
NEEDS_SOFTWARE_TOOLS ?= 0

include ../../common/make/quartus-version.mk

.PHONY: all project compile check_timing program-sof usb-wsl usb-windows clean

# all: creates the project and compiles in a single invocation.
all:
	$(call QTOOL, \
	  quartus_sh -t de10_nano_project.tcl && \
	  quartus_sh --flow compile $(PROJECT_NAME) -c $(REVISION_NAME) && \
	  python3 $(REPO_ROOT)/00_led_blinking/scripts/check_timing_slacks.py \
	    $(REVISION_NAME).sta.rpt 2>/dev/null || true)

project:
	$(call QTOOL,quartus_sh -t de10_nano_project.tcl)

compile:
	$(call QTOOL,quartus_sh --flow compile $(PROJECT_NAME) -c $(REVISION_NAME))

check_timing:
	$(call QTOOL,python3 $(REPO_ROOT)/00_led_blinking/scripts/check_timing_slacks.py \
	  $(REVISION_NAME).sta.rpt 2>/dev/null || true)

program-sof: usb-wsl $(REVISION_NAME).sof
	$(QUARTUS_PROGRAM)

clean:
	rm -rf db incremental_db output_files
	rm -f $(foreach ENDING,.rpt .summary .qsf .qpf .qws .done .smsg .jdi .pin .sld .sof dump.txt,$(wildcard *$(ENDING)))
```

> `$(REPO_ROOT)` resolves to the host repo path in local mode and to `/work` inside the Docker container (the repo is mounted at `/work`), so `python3 $(REPO_ROOT)/...` works in both runners.

- [ ] **Step 2: Verify compile with local 25.1**

Run:
```bash
cd 00_led_blinking/quartus
make clean
make all QUARTUS_VERSION=25.1
```
Expected: `quartus_sh` runs from `$HOME/tools/altera_lite/25.1std`, compile succeeds, `de10_nano.sof` produced, timing check runs.

- [ ] **Step 3: Verify fallback to Docker 23.1**

Run:
```bash
make clean
make all QUARTUS_VERSION=23.1
```
Expected: compiles inside `cvsoc/quartus:23.1`.

- [ ] **Step 4: Rewrite `01_led_running/quartus/Makefile`** (same shape as Step 1–2, with `PROJECT_NAME = 01_led_running`)

- [ ] **Step 5: Program the board with 25.1**

Run:
```bash
cd 01_led_running/quartus
make program-sof QUARTUS_VERSION=25.1 USBIPD_BUSID=8-3
```
Expected: `Configuration succeeded -- 1 device(s) configured`. LEDs run.

- [ ] **Step 6: Commit**

```bash
git add 00_led_blinking/quartus/Makefile 01_led_running/quartus/Makefile
git commit -m "refactor(00,01): migrate Makefiles to multi-version quartz fragment"
```

---

### Task 5: Migrate Nios II projects — 04/06/08 (23.1-only)

**Files:**
- Modify: `04_nios2_led/quartus/Makefile`, `06_nios2_interrupts/quartus/Makefile`, `08_nios2_debug/quartus/Makefile` (rewrite each)
- Test: full Nios II build under Docker 23.1 + negative test under 25.1

**Interfaces:**
- Consumes: fragment; `QTOOL` (docker 23.1), `usb-wsl`.
- Produces: `make all` builds qsys → compile → BSP → app under 23.1.

- [ ] **Step 1: Rewrite `04_nios2_led/quartus/Makefile`**

```make
# Makefile for the 04_nios2_led Quartus project.
# Nios II classic — supported only on Quartus 23.1 (Docker).
# Orchestrates: Platform Designer → Quartus compile → BSP → app build.

PROJECT_NAME  = 04_nios2_led
REVISION_NAME = de10_nano

QSYS_TCL     = ../qsys/nios2_system.tcl
QSYS_FILE    = ../qsys/nios2_system.qsys
QSYS_GEN_DIR = ../qsys/nios2_system_gen
SOPCINFO     = ../qsys/nios2_system.sopcinfo
BSP_DIR      = ../software/bsp
APP_DIR      = ../software/app

QUARTUS_SUPPORTED ?= 23.1
QUARTUS_DEFAULT   ?= 23.1
NEEDS_SOFTWARE_TOOLS ?= 0

include ../../common/make/quartus-version.mk

.PHONY: all qsys project compile bsp app check_timing \
        program-sof download-elf terminal usb-wsl usb-windows clean

all:
	$(call QTOOL, \
	  cd ../qsys && qsys-script --script=nios2_system.tcl && \
	  cd ../quartus && \
	  qsys-generate ../qsys/nios2_system.qsys --synthesis=VHDL --output-directory=../qsys/nios2_system_gen && \
	  quartus_sh -t de10_nano_project.tcl && \
	  quartus_sh --flow compile $(PROJECT_NAME) -c $(REVISION_NAME) && \
	  mkdir -p ../software/bsp && \
	  nios2-bsp-create-settings --sopc ../qsys/nios2_system.sopcinfo --type hal \
	    --settings ../software/bsp/settings.bsp --bsp-dir ../software/bsp \
	    --script /opt/intelFPGA/nios2eds/sdk2/bin/bsp-set-defaults.tcl --cpu-name nios2 && \
	  make -C ../software/bsp WINDOWS_EXE= && \
	  make -C ../software/app)

$(QSYS_FILE): $(QSYS_TCL)
	$(call QTOOL,cd ../qsys && qsys-script --script=nios2_system.tcl)

qsys: $(QSYS_FILE)
	$(call QTOOL,qsys-generate $(QSYS_FILE) --synthesis=VHDL --output-directory=$(QSYS_GEN_DIR))

project: $(QSYS_GEN_DIR)/synthesis/nios2_system.qip
	$(call QTOOL,quartus_sh -t de10_nano_project.tcl)

compile:
	$(call QTOOL,quartus_sh --flow compile $(PROJECT_NAME) -c $(REVISION_NAME))

BSP_DEFAULT_TCL = /opt/intelFPGA/nios2eds/sdk2/bin/bsp-set-defaults.tcl

bsp: $(SOPCINFO)
	$(call QTOOL, \
	  mkdir -p $(BSP_DIR) && \
	  nios2-bsp-create-settings \
	    --sopc $(SOPCINFO) --type hal \
	    --settings $(BSP_DIR)/settings.bsp --bsp-dir $(BSP_DIR) \
	    --script $(BSP_DEFAULT_TCL) --cpu-name nios2 && \
	  make -C $(BSP_DIR) WINDOWS_EXE=)

app: bsp
	$(call QTOOL,make -C $(APP_DIR))

check_timing:
	$(call QTOOL,python3 $(REPO_ROOT)/00_led_blinking/scripts/check_timing_slacks.py \
	  $(REVISION_NAME).sta.rpt 2>/dev/null || true)

program-sof: usb-wsl $(REVISION_NAME).sof
	$(QUARTUS_PROGRAM)

# download-elf / terminal need the uname shim and --privileged / -it mounts
# that QTOOL does not provide; they are written as raw docker run commands in
# Step 5 below.

clean:
	$(call QTOOL, \
	  rm -rf db incremental_db output_files && \
	  rm -rf $(QSYS_GEN_DIR) $(QSYS_FILE) && \
	  rm -rf $(BSP_DIR) && \
	  make -C $(APP_DIR) clean 2>/dev/null || true && \
	  find . -maxdepth 1 \( -name '*.rpt' -o -name '*.summary' -o -name '*.qsf' \
	    -o -name '*.qpf' -o -name '*.qws' -o -name '*.done' -o -name '*.smsg' \
	    -o -name '*.jdi' -o -name '*.pin' -o -name '*.sld' -o -name '*.sof' \
	    -o -name 'dump.txt' \) -delete)
```

> Nios II tools (`nios2-bsp-create-settings`, `nios2-download`, `nios2-terminal`) live in the 23.1 container's PATH, so `QTOOL` (docker) resolves them automatically.

- [ ] **Step 2: Verify full build under 23.1**

Run:
```bash
cd 04_nios2_led/quartus
make clean
make all QUARTUS_VERSION=23.1
```
Expected: qsys generation, compile, BSP build, and app build all succeed, producing `software/app/nios2_led.elf`.

- [ ] **Step 3: Negative test under 25.1**

Run: `make all QUARTUS_VERSION=25.1`
Expected: make aborts with `04_nios2_led does not support Quartus 25.1 (supported: 23.1)`.

- [ ] **Step 4: Rewrite `06_nios2_interrupts/quartus/Makefile`** (same shape; `PROJECT_NAME = 06_nios2_interrupts`, app ELF `nios2_interrupts.elf`; keep `gdb-server`/`gdb-tui` targets, replacing their `docker run` with `$(call QTOOL,...)` and the shim mount — see Step 5)

- [ ] **Step 5: Preserve the uname shim for 06/08 debug targets**

The `download-elf`, `terminal`, `gdb-server`, and `gdb` targets in 06/08 mount `$(UNAME_SHIM)` because the Altera scripts misdetect WSL inside Docker. Keep these targets as explicit `docker run` commands (they need the extra `-v` mount that `QTOOL` does not provide), but reference the fragment's `DOCKER_IMAGE` and `REPO_ROOT`. Example for `download-elf`:

```make
UNAME_SHIM = $(REPO_ROOT)/common/docker/uname_shim.sh

download-elf: usb-wsl $(APP_DIR)/nios2_interrupts.elf
	docker run --rm --user $$(id -u):$$(id -g) --privileged \
	  -v $(REPO_ROOT):/work \
	  -v $(UNAME_SHIM):/usr/local/bin/uname:ro \
	  $(DOCKER_IMAGE) \
	  nios2-download -g /work/06_nios2_interrupts/software/app/nios2_interrupts.elf
```

- [ ] **Step 6: Rewrite `08_nios2_debug/quartus/Makefile`** (same shape; reuses 06's qsys/bsp via its `all` target which calls `$(MAKE) -C $(REPO_ROOT)/06_nios2_interrupts/quartus all`)

- [ ] **Step 7: Verify 06 and 08 build under 23.1**

Run:
```bash
cd 06_nios2_interrupts/quartus && make all QUARTUS_VERSION=23.1
cd 08_nios2_debug/quartus && make all QUARTUS_VERSION=23.1
```
Expected: both produce their `.elf`.

- [ ] **Step 8: Commit**

```bash
git add 04_nios2_led/quartus/Makefile 06_nios2_interrupts/quartus/Makefile 08_nios2_debug/quartus/Makefile
git commit -m "refactor(04,06,08): migrate Nios II Makefiles to shared fragment (23.1-only)"
```

---

### Task 6: Migrate HPS projects — 05/07/09/14/15 (software tools split)

**Files:**
- Modify: `05_hps_led/quartus/Makefile`, `07_hps_interrupts/quartus/Makefile`, `09_hps_debug/quartus/Makefile`, `14_ddr3_hps_test/quartus/Makefile`, `15_ddr3_fpga_hps/quartus/Makefile`
- Test: build app via `SWTOOL`, FPGA via `QTOOL`

**Interfaces:**
- Consumes: fragment; `QTOOL` (FPGA), `SWTOOL` (ARM app), `usb-wsl`.
- Produces: FPGA bitstream + ARM ELF/bin.

- [ ] **Step 1: Rewrite `05_hps_led/quartus/Makefile`**

```make
# Makefile for the 05_hps_led Quartus project.
# Orchestrates: Platform Designer → Quartus compile → ARM app build (tools container).

PROJECT_NAME  = 05_hps_led
REVISION_NAME = de10_nano

QSYS_TCL     = ../qsys/hps_system.tcl
QSYS_FILE    = ../qsys/hps_system.qsys
QSYS_GEN_DIR = ../qsys/hps_system/synthesis
APP_DIR      = ../software/app
ARM_CC       ?= arm-linux-gnueabihf-gcc

QUARTUS_SUPPORTED ?= 23.1 25.1
QUARTUS_DEFAULT   ?= 23.1
NEEDS_SOFTWARE_TOOLS ?= 1

include ../../common/make/quartus-version.mk

HPS_IP   ?= 192.168.1.100
HPS_USER ?= root

.PHONY: all qsys patch-oct project compile app check_timing \
        program-sof download-elf deploy-elf usb-wsl usb-windows clean

all:
	$(call QTOOL, \
	  cd ../qsys && qsys-script --script=hps_system.tcl && \
	  cd ../quartus && \
	  qsys-generate ../qsys/hps_system.qsys --synthesis=VERILOG && \
	  python3 $(REPO_ROOT)/05_hps_led/scripts/patch_oct.py \
	    ../qsys/hps_system/synthesis/submodules/altdq_dqs2_acv_connect_to_hard_phy_cyclonev.sv && \
	  quartus_sh -t de10_nano_project.tcl && \
	  quartus_map --read_settings_files=on --write_settings_files=off $(PROJECT_NAME) -c $(REVISION_NAME) && \
	  quartus_sta -t ../qsys/hps_system/synthesis/submodules/hps_sdram_p0_pin_assignments.tcl $(REVISION_NAME) && \
	  quartus_map --read_settings_files=on --write_settings_files=off $(PROJECT_NAME) -c $(REVISION_NAME) && \
	  quartus_fit --read_settings_files=on --write_settings_files=off $(PROJECT_NAME) -c $(REVISION_NAME) && \
	  quartus_asm --read_settings_files=on --write_settings_files=off $(PROJECT_NAME) -c $(REVISION_NAME) && \
	  quartus_sta $(PROJECT_NAME) -c $(REVISION_NAME))
	$(call SWTOOL,make -C software/app CC=$(ARM_CC))

$(QSYS_FILE): $(QSYS_TCL)
	$(call QTOOL,cd ../qsys && qsys-script --script=hps_system.tcl)

qsys: $(QSYS_FILE)
	$(call QTOOL, \
	  qsys-generate $(QSYS_FILE) --synthesis=VERILOG && \
	  python3 $(REPO_ROOT)/05_hps_led/scripts/patch_oct.py \
	    ../qsys/hps_system/synthesis/submodules/altdq_dqs2_acv_connect_to_hard_phy_cyclonev.sv)

patch-oct:
	$(call QTOOL,python3 $(REPO_ROOT)/05_hps_led/scripts/patch_oct.py \
	  ../qsys/hps_system/synthesis/submodules/altdq_dqs2_acv_connect_to_hard_phy_cyclonev.sv)

project: $(QSYS_GEN_DIR)/hps_system.qip
	$(call QTOOL,quartus_sh -t de10_nano_project.tcl)

compile:
	$(call QTOOL, \
	  quartus_map --read_settings_files=on --write_settings_files=off \
	    $(PROJECT_NAME) -c $(REVISION_NAME) && \
	  quartus_sta -t $(QSYS_GEN_DIR)/submodules/hps_sdram_p0_pin_assignments.tcl \
	    $(REVISION_NAME) && \
	  quartus_map --read_settings_files=on --write_settings_files=off \
	    $(PROJECT_NAME) -c $(REVISION_NAME) && \
	  quartus_fit --read_settings_files=on --write_settings_files=off \
	    $(PROJECT_NAME) -c $(REVISION_NAME) && \
	  quartus_asm --read_settings_files=on --write_settings_files=off \
	    $(PROJECT_NAME) -c $(REVISION_NAME) && \
	  quartus_sta $(PROJECT_NAME) -c $(REVISION_NAME))

app:
	$(call SWTOOL,make -C software/app CC=$(ARM_CC))

check_timing:
	$(call QTOOL,python3 $(REPO_ROOT)/00_led_blinking/scripts/check_timing_slacks.py \
	  $(REVISION_NAME).sta.rpt 2>/dev/null || true)

program-sof: usb-wsl $(REVISION_NAME).sof
	$(QUARTUS_PROGRAM)

# download-elf: OpenOCD on the host with the DE10-Nano USB-Blaster II config.
OPENOCD_UBLAST_CFG = ../scripts/de10_nano_hps_ublast2.cfg
BLASTER_FW = $(REPO_ROOT)/12_zephyr_led/zephyr/boards/intel/socfpga_std/cyclonev_socdk/support/blaster_6810.hex
APP_BIN    = $(REPO_ROOT)/$(PROJECT_NAME)/software/app/hps_led.bin

download-elf: usb-wsl $(APP_DIR)/hps_led.elf
	openocd \
	  -c "set BLASTER_FW $(BLASTER_FW)" \
	  -f $(realpath $(OPENOCD_UBLAST_CFG)) \
	  -c "init" \
	  -c "fpgasoc_prepare_ocram_exec" \
	  -c "load_image $(APP_BIN) 0xFFFF0000 bin" \
	  -c "resume 0xFFFF0000" \
	  -c "shutdown"
	@echo "Done: HPS LED application is running."

deploy-elf: $(APP_DIR)/hps_led.elf
	scp $(APP_DIR)/hps_led.elf $(APP_DIR)/hps_led.bin \
	    $(HPS_USER)@$(HPS_IP):/tmp/
	@echo "ELF + binary copied to $(HPS_USER)@$(HPS_IP):/tmp/"

clean:
	$(call QTOOL, \
	  rm -rf db incremental_db output_files hps_isw_handoff && \
	  rm -rf $(QSYS_FILE) ../qsys/hps_system ../qsys/hps_system.sopcinfo && \
	  make -C $(APP_DIR) clean 2>/dev/null || true && \
	  find . -maxdepth 1 \( -name '*.rpt' -o -name '*.summary' -o -name '*.qsf' \
	    -o -name '*.qpf' -o -name '*.qws' -o -name '*.done' -o -name '*.smsg' \
	    -o -name '*.jdi' -o -name '*.pin' -o -name '*.sld' -o -name '*.sof' \
	    -o -name 'dump.txt' \) -delete)
```

> Note: `download-elf` and `deploy-elf` use host `openocd`/`scp` (pre-existing behavior for 05). If the host lacks `openocd`, wrap it in `$(call SWTOOL,...)` instead (see Step 5).

- [ ] **Step 2: Verify FPGA build with 25.1 local**

Run:
```bash
cd 05_hps_led/quartus
make clean
make project compile QUARTUS_VERSION=25.1
```
Expected: FPGA compile succeeds with local 25.1.

- [ ] **Step 3: Verify ARM app via tools container**

Run:
```bash
make app QUARTUS_VERSION=25.1
```
Expected: `hps_led.elf` + `hps_led.bin` built inside `cvsoc/tools:1.0`.

- [ ] **Step 4: Verify Docker 23.1 fallback**

Run: `make all QUARTUS_VERSION=23.1` — expected full FPGA+ARM build under 23.1 with SWTOOL still using the tools container for the ARM step.

- [ ] **Step 5: Route openocd/scp through SWTOOL for consistency**

For 09 (and any project that ran openocd/gdb inside the 23.1 container), wrap those invocations in `$(call SWTOOL, ...)`. For 05, `download-elf`/`deploy-elf` may stay host-side if host tools exist; otherwise convert:

```make
download-elf: usb-wsl $(APP_DIR)/hps_led.elf
	$(call SWTOOL, \
	  openocd \
	    -c "set BLASTER_FW $(BLASTER_FW)" \
	    -f $(REPO_ROOT)/05_hps_led/scripts/de10_nano_hps_ublast2.cfg \
	    -c "init" -c "fpgasoc_prepare_ocram_exec" \
	    -c "load_image $(REPO_ROOT)/05_hps_led/software/app/hps_led.bin 0xFFFF0000 bin" \
	    -c "resume 0xFFFF0000" -c "shutdown")
```

- [ ] **Step 6: Rewrite `07_hps_interrupts`, `09_hps_debug`, `14_ddr3_hps_test`, `15_ddr3_fpga_hps` Makefiles** using the same pattern; keep their project-specific targets (`gdb`/`openocd`/`gdb-server` for 09, DDR-specific pin TCL for 14/15).

- [ ] **Step 7: Verify all HPS projects compile under 25.1 + tools container**

Run for each of 05/07/09/14/15:
```bash
make project compile QUARTUS_VERSION=25.1
make app QUARTUS_VERSION=25.1
```
Expected: all FPGA bitstreams + ARM binaries build.

- [ ] **Step 8: Commit**

```bash
git add 05_hps_led/quartus/Makefile 07_hps_interrupts/quartus/Makefile \
        09_hps_debug/quartus/Makefile 14_ddr3_hps_test/quartus/Makefile \
        15_ddr3_fpga_hps/quartus/Makefile
git commit -m "refactor(05,07,09,14,15): migrate HPS Makefiles to shared fragment + tools container"
```

---

### Task 7: Migrate root-Makefile projects — 10, 11, 12, 13

**Files:**
- Modify: `10_linux_led/Makefile`, `11_ethernet_hps_led/Makefile`, `12_zephyr_led/Makefile`, `13_secure_boot/Makefile`

**Interfaces:**
- Consumes: fragment (`QTOOL` for `quartus_cpf`/`quartus_sh` steps; `SWTOOL` where ARM cross-compile needed).

- [ ] **Step 1: Update `10_linux_led/Makefile`**

Add the fragment include and switch the `.rbf` conversion step to `QTOOL`:

```make
QUARTUS_SUPPORTED ?= 23.1 25.1
NEEDS_SOFTWARE_TOOLS ?= 0
include ../common/make/quartus-version.mk
```

Replace the `rbf` docker invocation:

```make
$(RBF_FILE): $(SOF_FILE) scripts/convert_sof_to_rbf.sh
	$(call QTOOL, \
	  cd $(REPO_ROOT)/10_linux_led && \
	  bash scripts/convert_sof_to_rbf.sh \
	    $(REPO_ROOT)/05_hps_led/quartus/de10_nano.sof $(REPO_ROOT)/10_linux_led/de10_nano.rbf)
	@echo "FPGA bitstream: $(RBF_FILE)"
```

- [ ] **Step 2: Verify `rbf` under 25.1**

Run: `make rbf QUARTUS_VERSION=25.1`
Expected: `de10_nano.rbf` produced via local `quartus_cpf`.

- [ ] **Step 3: Update `11_ethernet_hps_led/Makefile`** (same fragment include; switch `server-cross`/`server-pb-cross` ARM compiles to `$(call SWTOOL, ...)` with `NEEDS_SOFTWARE_TOOLS ?= 1`)

- [ ] **Step 4: Verify server cross-compiles**

Run: `make server-cross QUARTUS_VERSION=25.1` and `make server-pb-cross QUARTUS_VERSION=25.1`
Expected: `led_server` and `led_server_pb` binaries built in `cvsoc/tools:1.0`.

- [ ] **Step 5: Update `12_zephyr_led/Makefile`** — Zephyr uses its own west toolchain; only add the fragment include for consistency (`QUARTUS_SUPPORTED ?= 23.1 25.1`, `NEEDS_SOFTWARE_TOOLS ?= 0`) and keep all west targets as-is. No `QTOOL` needed.

- [ ] **Step 6: Update `13_secure_boot/Makefile`** — add fragment include; switch the `encrypt` step to `QTOOL`:

```make
QUARTUS_SUPPORTED ?= 23.1 25.1
NEEDS_SOFTWARE_TOOLS ?= 0
include ../common/make/quartus-version.mk

$(OUTPUT_RBF): $(KEY_FILE) $(INPUT_SOF) scripts/encrypt_bitstream.sh
	$(call QTOOL, \
	  cd $(REPO_ROOT)/13_secure_boot && \
	  bash scripts/encrypt_bitstream.sh \
	    $(KEY_FILE) $(KEY_ID) $(REPO_ROOT)/05_hps_led/quartus/de10_nano.sof $(OUTPUT_RBF))
```

- [ ] **Step 7: Verify `make keys` + `make rsa_keys` + `make sign_fit`**

Run in `13_secure_boot`: `make keys rsa_keys sign_fit`
Expected: key files generated locally (no Quartus needed).

- [ ] **Step 8: Commit**

```bash
git add 10_linux_led/Makefile 11_ethernet_hps_led/Makefile \
        12_zephyr_led/Makefile 13_secure_boot/Makefile
git commit -m "refactor(10,11,12,13): migrate root Makefiles to shared fragment"
```

---

### Task 8: Nios V sibling project — `04b_niosv_led` (25.1-only)

**Files:**
- Create: `04b_niosv_led/qsys/niosv_system.tcl`
- Create: `04b_niosv_led/quartus/Makefile`, `04b_niosv_led/quartus/de10_nano_project.tcl`, `04b_niosv_led/quartus/de10_nano_pin_assignments.tcl`, `04b_niosv_led/quartus/de10_nano.sdc`
- Create: `04b_niosv_led/software/app/main.c`, `04b_niosv_led/software/app/Makefile`

**Interfaces:**
- Consumes: fragment (`QTOOL` local 25.1, `NIOSV` tools via PATH).
- Produces: Nios V/m LED demo that builds under 25.1 with the RISC-V toolchain.

- [ ] **Step 1: Discovery — confirm RISC-V toolchain availability**

Nios V software builds need the RiscFree RISC-V toolchain, which is NOT bundled in 25.1 (only the IP and `niosv-bsp`/`niosv-app`/`niosv-download` shells ship). Run:
```bash
ls $HOME/tools/altera_lite/25.1std/niosv/bin/niosv-bsp
find $HOME -maxdepth 4 -type d -iname "*riscfree*" 2>/dev/null
find $HOME -maxdepth 4 -type d -iname "riscv32-unknown-elf*" 2>/dev/null
```
If no toolchain is found, document that `04b_niosv_led` requires the RiscFree toolchain install, and keep the hardware (qsys + Quartus compile) buildable while the app build is documented as pending toolchain install. Do not block Tasks 1–7 on this.

- [ ] **Step 2: Write `qsys/niosv_system.tcl`**

Base it on `04_nios2_led/qsys/nios2_system.tcl` but replace the CPU:
- Component name: `intel_niosv_m` (Nios V/m), instance name `niosv`.
- Parameters to set for Nios V/m (verify against the IP's `intel_niosv_m_hw.tcl` in `$HOME/tools/altera_lite/25.1std/ip/altera/soft_processor/intel_niosv_m/`):
  - `set_instance_parameter_value niosv "hardware.multiplier" {None}` etc. — use defaults for a minimal core; the master/slave Avalon interfaces are named `data_master` and `instruction_master`, and the IRQ receiver is `niosv.irq`.

- [ ] **Step 3: Write `quartus/de10_nano_project.tcl`** mirroring 04's, referencing `niosv_system_gen` and the Nios V qip.

- [ ] **Step 4: Copy pin assignments + SDC from 04** (`de10_nano_pin_assignments.tcl`, `de10_nano.sdc`).

- [ ] **Step 5: Write `quartus/Makefile`**

```make
# Makefile for the 04b_niosv_led Quartus project (Nios V/m on 25.1).

PROJECT_NAME  = 04b_niosv_led
REVISION_NAME = de10_nano

QSYS_TCL     = ../qsys/niosv_system.tcl
QSYS_FILE    = ../qsys/niosv_system.qsys
QSYS_GEN_DIR = ../qsys/niosv_system_gen
SOPCINFO     = ../qsys/niosv_system.sopcinfo
BSP_DIR      = ../software/bsp
APP_DIR      = ../software/app

QUARTUS_SUPPORTED ?= 25.1
QUARTUS_DEFAULT   ?= 25.1
NEEDS_SOFTWARE_TOOLS ?= 0

include ../../common/make/quartus-version.mk

.PHONY: all qsys project compile bsp app program-sof download-elf terminal usb-wsl clean

all: project compile bsp app

$(QSYS_FILE): $(QSYS_TCL)
	$(call QTOOL,cd ../qsys && qsys-script --script=niosv_system.tcl)

qsys: $(QSYS_FILE)
	$(call QTOOL,qsys-generate $(QSYS_FILE) --synthesis=VHDL --output-directory=$(QSYS_GEN_DIR))

project: $(QSYS_GEN_DIR)/synthesis/niosv_system.qip
	$(call QTOOL,quartus_sh -t de10_nano_project.tcl)

compile:
	$(call QTOOL,quartus_sh --flow compile $(PROJECT_NAME) -c $(REVISION_NAME))

bsp: $(SOPCINFO)
	$(call QTOOL, \
	  mkdir -p $(BSP_DIR) && \
	  niosv-bsp -c --sopcinfo=$(SOPCINFO) --type=hal \
	    --bsp-dir=$(BSP_DIR) --settings=$(BSP_DIR)/settings.bsp \
	    --cpu-instance=niosv)

app: bsp
	$(call QTOOL, \
	  niosv-app --bsp-dir=$(BSP_DIR) --srcs=$(APP_DIR)/main.c \
	    --app-dir=$(APP_DIR) --elf-name=niosv_led.elf && \
	  cd $(APP_DIR) && cmake -S . -B build && cmake --build build)

program-sof: usb-wsl $(REVISION_NAME).sof
	$(QUARTUS_PROGRAM)

download-elf: usb-wsl
	$(call QTOOL,niosv-download --go $(REPO_ROOT)/$(PROJECT_NAME)/software/app/build/niosv_led.elf)

terminal: usb-wsl
	$(call QTOOL,niosv-terminal)

clean:
	$(call QTOOL, \
	  rm -rf db incremental_db output_files && \
	  rm -rf $(QSYS_GEN_DIR) $(QSYS_FILE) $(BSP_DIR) $(APP_DIR)/build)
```

> Verify the exact `niosv-bsp`/`niosv-app` flags against `niosv-bsp --help` before finalizing; Nios V uses a CMake-based build (not the Nios II make flow).

- [ ] **Step 6: Write `software/app/main.c`** — mirror 04's `main.c` (LED PIO base address from the new system's address map; the HAL API is the same as Nios II HAL).

- [ ] **Step 7: Verify qsys + FPGA compile under 25.1**

Run:
```bash
cd 04b_niosv_led/quartus
make qsys project compile QUARTUS_VERSION=25.1
```
Expected: Platform Designer generates the Nios V/m system and Quartus 25.1 compiles the bitstream.

- [ ] **Step 8: If RISC-V toolchain is available, verify `make bsp app`; otherwise document as pending.**

- [ ] **Step 9: Commit**

```bash
git add 04b_niosv_led
git commit -m "feat(04b): add Nios V/m LED project for Quartus 25.1"
```

---

### Task 9: Nios V sibling projects — `06b_niosv_interrupts`, `08b_niosv_debug`

**Files:**
- Create: `06b_niosv_interrupts/` and `08b_niosv_debug/` mirroring 04b, adding interrupt and GDB-server targets.

**Interfaces:**
- Consumes: fragment, patterns from 04b and 06/08.

- [ ] **Step 1: Create `06b_niosv_interrupts`** by copying 04b and adding an interrupt-capable PIO + timer in `niosv_system.tcl`, plus `gdb-server`/`gdb-tui` targets (Nios V uses `niosv-download` + RISC-V GDB; verify flags).

- [ ] **Step 2: Create `08b_niosv_debug`** reusing 06b's qsys/BSP (like 08 reuses 06), with `download-elf`/`gdb` targets.

- [ ] **Step 3: Verify `make qsys project compile QUARTUS_VERSION=25.1` for both.**

- [ ] **Step 4: Commit**

```bash
git add 06b_niosv_interrupts 08b_niosv_debug
git commit -m "feat(06b,08b): add Nios V interrupt/debug projects for Quartus 25.1"
```

---

### Task 10: Documentation

**Files:**
- Create: `docs/tutorial_phase11_multi_version_quartus.md`
- Modify: `docs/tutorial_docker_dev_environment.md`, `00_led_blinking/doc/README.md`, project READMEs as needed

- [ ] **Step 1: Write the multi-version tutorial** covering: how version selection works, the manifest variables, how to add a new Quartus version (add a probe root), the tools container, and Nios II vs Nios V guidance.

- [ ] **Step 2: Update `docs/tutorial_docker_dev_environment.md`** to note the new `cvsoc/tools:1.0` image and that Quartus now runs per-version.

- [ ] **Step 3: Update project READMEs' build/program commands** to show `make all` (auto-detect) and `make QUARTUS_VERSION=25.1 all`.

- [ ] **Step 4: Commit**

```bash
git add docs/ 00_led_blinking/doc/README.md
git commit -m "docs: document multi-version Quartus support"
```

---

### Task 11: Final verification sweep

**Files:** none (verification only)

- [ ] **Step 1: Auto-detect regression** — unset `QUARTUS_VERSION`; confirm `make print`-style detection picks local 25.1 for a `23.1 25.1` project and Docker 23.1 for Nios II projects.

- [ ] **Step 2: Full-matrix compile** — for each migrated project run its primary build with the correct default version.

- [ ] **Step 3: Board verification** — program 00, 01, 05 with 25.1; program 04 with 23.1; confirm LEDs.

- [ ] **Step 4: Update `docs/superpowers/specs/2026-08-02-multi-version-quartus-design.md` status to "Implemented"** and commit.
