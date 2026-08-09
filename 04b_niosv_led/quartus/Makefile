# Makefile for the 04b_niosv_led Quartus project (Nios V/m on 25.1).
# Orchestrates: Platform Designer → Quartus compile → BSP → app build.
# The RISC-V (RiscFree) toolchain is required by niosv-bsp / niosv-app; until
# it is installed the hardware targets (qsys project compile) build and the
# software targets fail with a clear toolchain error.

PROJECT_NAME  = 04b_niosv_led
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

.PHONY: all qsys project compile bsp app program-sof download-elf terminal usb-wsl clean

all: compile app

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

# The .bsp settings file is the positional argument to niosv-bsp (there is no
# --settings option). Requires the RiscFree RISC-V toolchain + CMake.
bsp: $(QSYS_STAMP)
	$(call QTOOL,$(NIOSV_ENV) \
	  mkdir -p $(BSP_DIR) && \
	  niosv-bsp -c --sopcinfo=$(SOPCINFO) --type=hal \
	    --bsp-dir=$(BSP_DIR) --cpu-instance=niosv \
	    $(BSP_DIR)/settings.bsp)

app: bsp
	$(call QTOOL,$(NIOSV_ENV) \
	  niosv-app --bsp-dir=$(BSP_DIR) --srcs=$(APP_DIR)/main.c \
	    --app-dir=$(APP_DIR) --elf-name=niosv_led.elf && \
	  cd $(APP_DIR) && cmake -S . -B build \
	    -DCMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE) && cmake --build build)

program-sof: usb-ready $(REVISION_NAME).sof
	$(QUARTUS_PROGRAM)

download-elf: usb-ready
	$(call QTOOL,$(NIOSV_ENV) \
	  niosv-download --go $(QROOT)/$(PROJECT_NAME)/software/app/build/niosv_led.elf)

terminal: usb-ready
	$(call QTOOL,$(NIOSV_ENV) niosv-terminal)

clean:
	$(call QTOOL, \
	  rm -rf db incremental_db output_files && \
	  rm -rf $(QSYS_GEN_DIR) $(QSYS_FILE) $(SOPCINFO) $(BSP_DIR) $(APP_DIR)/build)
