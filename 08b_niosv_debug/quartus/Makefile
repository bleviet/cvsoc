# Makefile for the 08b_niosv_debug Quartus project (Nios V/m on 25.1).
# Reuses the 06b_niosv_interrupts hardware + BSP; adds download-elf and RISC-V
# GDB interactive debug targets on top of that flow.
# The RISC-V (RiscFree) toolchain is required by niosv-app / the RISC-V GDB
# client; until it is installed the hardware targets build and the software /
# debug targets fail with a clear toolchain error.

PROJECT_NAME  = 08b_niosv_debug
REVISION_NAME = de10_nano

QSYS_TCL     = ../qsys/niosv_system.tcl
QSYS_FILE    = ../qsys/niosv_system.qsys
QSYS_GEN_DIR = ../qsys/niosv_system_gen
QSYS_QIP     = $(QSYS_GEN_DIR)/synthesis/niosv_system.qip
QSYS_STAMP   = $(QSYS_GEN_DIR)/.generated
SOPCINFO     = ../qsys/niosv_system.sopcinfo
BSP_DIR      = ../software/bsp
APP_DIR      = ../software/app
CMAKE_BUILD_TYPE ?= Release

QUARTUS_SUPPORTED ?= 25.1
QUARTUS_DEFAULT   ?= 25.1
NEEDS_SOFTWARE_TOOLS ?= 0

include ../../common/make/quartus-version.mk

# niosv-bsp/niosv-app need the Quartus environment vars; the local runner's
# QTOOL only prepends bin dirs to PATH. The Docker image has these baked in.
ifeq ($(QUARTUS_RUNNER),local)
  RISCFREE_ROOT = $(QUARTUS_HOME)/riscfree
  RISCFREE_PATH = $(RISCFREE_ROOT)/build_tools/bin:$(RISCFREE_ROOT)/build_tools/cmake/bin:$(RISCFREE_ROOT)/toolchain/riscv32-unknown-elf/bin:$(RISCFREE_ROOT)/RiscFree:$(RISCFREE_ROOT)/debugger/gdbserver-riscv
  NIOSV_ENV = export QUARTUS_ROOTDIR=$(QUARTUS_HOME)/quartus; \
              export SOPC_KIT_NIOS2=$(QUARTUS_HOME)/nios2eds; \
              export PATH="$(RISCFREE_PATH):$$PATH";
else
  NIOSV_ENV =
endif

# RISC-V GDB server port (ash-riscv-gdb-server listens here; RISC-V GDB
# clients connect to it). Requires the RiscFree toolchain.
GDB_PORT ?= 2345

.PHONY: all qsys project compile bsp app program-sof download-elf terminal \
        usb-wsl close-gdb-port gdb-server gdb clean

# all: reuses Phase 4 (06b_niosv_interrupts) hardware + BSP, then copies the
# bitstream/BSP into this project and builds the debug application. The app
# build is best-effort (RiscFree toolchain not installed) so 'make all' still
# yields the bitstream and BSP copies.
all:
	@echo "═══════════════════════════════════════════════════════════════"
	@echo "  08b_niosv_debug reuses 06b_niosv_interrupts hardware + BSP"
	@echo "═══════════════════════════════════════════════════════════════"
	$(MAKE) -C $(REPO_ROOT)/06b_niosv_interrupts/quartus all QUARTUS_VERSION=$(QUARTUS_VERSION)
	@echo "Copying bitstream and BSP from 06b_niosv_interrupts..."
	$(call QTOOL,$(NIOSV_ENV) \
	  mkdir -p $(QROOT)/$(PROJECT_NAME)/software/bsp $(QROOT)/$(PROJECT_NAME)/software/app && \
	  rm -f $(QROOT)/$(PROJECT_NAME)/quartus/$(REVISION_NAME).sof && \
	  cp $(QROOT)/06b_niosv_interrupts/quartus/de10_nano.sof $(QROOT)/$(PROJECT_NAME)/quartus/$(REVISION_NAME).sof && \
	  rm -rf $(QROOT)/$(PROJECT_NAME)/software/bsp/* && \
	  cp -r $(QROOT)/06b_niosv_interrupts/software/bsp/* $(QROOT)/$(PROJECT_NAME)/software/bsp/ && \
	  niosv-app --bsp-dir=$(QROOT)/$(PROJECT_NAME)/software/bsp \
	    --srcs=$(QROOT)/$(PROJECT_NAME)/software/app/main.c \
	    --app-dir=$(QROOT)/$(PROJECT_NAME)/software/app \
	    --elf-name=niosv_debug.elf && \
	  make -C $(QROOT)/$(PROJECT_NAME)/software/app) || { echo; \
	  echo "WARNING: 'make app' requires the RiscFree RISC-V toolchain"; \
	  echo "  (not installed) — bitstream de10_nano.sof and BSP were copied."; \
	  echo; }

$(QSYS_FILE): $(QSYS_TCL)
	$(call QTOOL,cd ../qsys && qsys-script --script=niosv_system.tcl)

$(QSYS_STAMP): $(QSYS_FILE)
	$(call QTOOL, \
	  qsys-generate $(QSYS_FILE) --synthesis=VHDL --output-directory=$(QSYS_GEN_DIR) && \
	  test -f $(QSYS_QIP) && test -f $(SOPCINFO) && touch $(QSYS_STAMP))

qsys: $(QSYS_STAMP)

project: $(QSYS_STAMP)
	$(call QTOOL,quartus_sh -t de10_nano_project.tcl)

compile: project
	$(call QTOOL,quartus_sh --flow compile $(PROJECT_NAME) -c $(REVISION_NAME))

# bsp/app stand-alone: the .sopcinfo comes from the local qsys copy; in
# practice the 06b-generated BSP is reused via 'all'.
bsp: $(QSYS_STAMP)
	$(call QTOOL,$(NIOSV_ENV) \
	  mkdir -p $(BSP_DIR) && \
	  niosv-bsp -c --sopcinfo=$(SOPCINFO) --type=hal \
	    --bsp-dir=$(BSP_DIR) --cpu-instance=niosv \
	    $(BSP_DIR)/settings.bsp)

app: bsp
	$(call QTOOL,$(NIOSV_ENV) \
	  niosv-app --bsp-dir=$(BSP_DIR) --srcs=$(APP_DIR)/main.c \
	    --app-dir=$(APP_DIR) --elf-name=niosv_debug.elf && \
	  cd $(APP_DIR) && cmake -S . -B build \
	    -DCMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE) && cmake --build build)

program-sof: usb-ready $(REVISION_NAME).sof
	$(QUARTUS_PROGRAM)

download-elf: usb-ready
	$(call QTOOL,$(NIOSV_ENV) \
	  niosv-download --go $(QROOT)/$(PROJECT_NAME)/software/app/build/niosv_debug.elf)

terminal: usb-ready
	$(call QTOOL,$(NIOSV_ENV) juart-terminal)

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
# connect. Run this in one terminal, then run 'make gdb' in a second terminal.
# Requires the RiscFree toolchain on PATH (niosv-shell).
gdb-server: close-gdb-port usb-ready
	$(call QTOOL,$(NIOSV_ENV) \
	  ash-riscv-gdb-server --auto-detect true --probe-type usb-blaster-2 \
	    --gdb-port $(GDB_PORT))

# ── Interactive RISC-V GDB session ────────────────────────────────────────────
# Connects riscv32-unknown-elf-gdb to the running gdb-server (started with
# 'make gdb-server'). The GDB init script loads the ELF, sets a breakpoint at
# set_led(), and continues. Requires the RiscFree toolchain.
gdb: usb-ready
	$(call QTOOL,$(NIOSV_ENV) \
	  riscv32-unknown-elf-gdb \
	    -ex "set pagination off" \
	    -ex "set arch riscv:rv32" \
	    -x $(QROOT)/$(PROJECT_NAME)/scripts/niosv_debug.gdb \
	    $(QROOT)/$(PROJECT_NAME)/software/app/build/niosv_debug.elf)

clean:
	$(call QTOOL, \
	  rm -rf db incremental_db output_files && \
	  rm -rf $(QSYS_GEN_DIR) $(QSYS_FILE) $(SOPCINFO) $(BSP_DIR) $(APP_DIR)/build && \
	  rm -f $(REVISION_NAME).sof)
