# Makefile for the 06b_niosv_interrupts Quartus project (Nios V/m on 25.1).
# Extends 04b_niosv_led with a 2-bit button PIO + edge-capture IRQ and adds
# RISC-V GDB server / TUI debug targets.
# Orchestrates: Platform Designer → Quartus compile → BSP → app build.
# The RISC-V (RiscFree) toolchain is required by niosv-bsp / niosv-app and by
# the RISC-V GDB server/client; until it is installed the hardware targets
# (qsys project compile) build and the software/debug targets fail with a
# clear toolchain error.

PROJECT_NAME  = 06b_niosv_interrupts
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

# niosv-bsp/niosv-app need the Quartus environment vars; the local runner's
# QTOOL only prepends bin dirs to PATH. The Docker image has these baked in.
ifeq ($(QUARTUS_RUNNER),local)
  NIOSV_ENV = export QUARTUS_ROOTDIR=$(QUARTUS_HOME)/quartus; \
              export SOPC_KIT_NIOS2=$(QUARTUS_HOME)/quartus;
else
  NIOSV_ENV =
endif

# RISC-V GDB server port (ash-riscv-gdb-server listens here; RISC-V GDB
# clients connect to it). Requires the RiscFree toolchain.
GDB_PORT ?= 2345

.PHONY: all qsys project compile bsp app program-sof download-elf terminal \
        usb-wsl close-gdb-port gdb-server gdb-tui clean

# Full pipeline. The app step is best-effort: without the RiscFree RISC-V
# toolchain it prints a warning and continues, so 'make all' still yields the
# bitstream. 'make app' alone still hard-fails so the toolchain gap is visible.
# (This lets 08b_niosv_debug reuse this project via '$(MAKE) -C ... all'.)
all: qsys project compile bsp
	$(MAKE) app || { echo; \
	  echo "WARNING: 'make app' requires the RiscFree RISC-V toolchain"; \
	  echo "  (not installed) — FPGA bitstream de10_nano.sof is complete."; \
	  echo; }

$(QSYS_FILE): $(QSYS_TCL)
	$(call QTOOL,cd ../qsys && qsys-script --script=niosv_system.tcl)

qsys: $(QSYS_FILE)
	$(call QTOOL,qsys-generate $(QSYS_FILE) --synthesis=VHDL --output-directory=$(QSYS_GEN_DIR))

project: $(QSYS_GEN_DIR)/synthesis/niosv_system.qip
	$(call QTOOL,quartus_sh -t de10_nano_project.tcl)

compile:
	$(call QTOOL,quartus_sh --flow compile $(PROJECT_NAME) -c $(REVISION_NAME))

# The .bsp settings file is the positional argument to niosv-bsp (there is no
# --settings option). Requires the RiscFree RISC-V toolchain + CMake.
bsp: $(SOPCINFO)
	$(call QTOOL,$(NIOSV_ENV) \
	  mkdir -p $(BSP_DIR) && \
	  niosv-bsp -c --sopcinfo=$(SOPCINFO) --type=hal \
	    --bsp-dir=$(BSP_DIR) --cpu-instance=niosv \
	    $(BSP_DIR)/settings.bsp)

app: bsp
	$(call QTOOL,$(NIOSV_ENV) \
	  niosv-app --bsp-dir=$(BSP_DIR) --srcs=$(APP_DIR)/main.c \
	    --app-dir=$(APP_DIR) --elf-name=niosv_interrupts.elf && \
	  cd $(APP_DIR) && cmake -S . -B build && cmake --build build)

program-sof: usb-ready $(REVISION_NAME).sof
	$(QUARTUS_PROGRAM)

download-elf: usb-ready
	$(call QTOOL,niosv-download --go $(QROOT)/$(PROJECT_NAME)/software/app/build/niosv_interrupts.elf)

terminal: usb-ready
	$(call QTOOL,niosv-terminal)

# ── RISC-V GDB server ─────────────────────────────────────────────────────────
# Nios V is debugged with the Ashling RiscFree GDB server (ash-riscv-gdb-server)
# plus a RISC-V GDB client, both shipped with the RiscFree toolchain. Until
# RiscFree is installed these targets fail with "command not found"; the FPGA
# build is unaffected.
close-gdb-port:
	@PORT_CONTAINERS=$$(docker ps --format '{{.ID}} {{.Ports}}' | \
	  grep -E "(0\\.0\\.0\\.0|\\[::\\]):$(GDB_PORT)->$(GDB_PORT)/tcp" | \
	  awk '{print $$1}'); \
	if [ -n "$$PORT_CONTAINERS" ]; then \
	  echo "Stopping container(s) using port $(GDB_PORT): $$PORT_CONTAINERS"; \
	  docker stop $$PORT_CONTAINERS >/dev/null; \
	else \
	  echo "Port $(GDB_PORT) is free (no Docker container bound)."; \
	fi

# Starts ash-riscv-gdb-server and keeps it running so a RISC-V GDB client can
# connect. Run this in one terminal, then run 'make gdb-tui' in a second
# terminal. Requires the RiscFree toolchain on PATH (niosv-shell).
gdb-server: close-gdb-port usb-ready
	$(call QTOOL,$(NIOSV_ENV) \
	  ash-riscv-gdb-server --auto-detect true --probe-type usb-blaster-2 \
	    --gdb-port $(GDB_PORT))

# ── RISC-V GDB TUI session ────────────────────────────────────────────────────
# Opens riscv32-unknown-elf-gdb in TUI mode. Requires gdb-server to be running
# first (make gdb-server in another terminal) and the RiscFree toolchain.
gdb-tui: usb-ready
	$(call QTOOL,$(NIOSV_ENV) \
	  riscv32-unknown-elf-gdb -tui \
	    -ex "set pagination off" \
	    -ex "set arch riscv:rv32" \
	    -x $(QROOT)/$(PROJECT_NAME)/scripts/niosv_interrupts.gdb \
	    $(QROOT)/$(PROJECT_NAME)/software/app/build/niosv_interrupts.elf)

clean:
	$(call QTOOL, \
	  rm -rf db incremental_db output_files && \
	  rm -rf $(QSYS_GEN_DIR) $(QSYS_FILE) $(BSP_DIR) $(APP_DIR)/build)
