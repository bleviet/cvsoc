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

UNAME_SHIM = $(REPO_ROOT)/common/docker/uname_shim.sh

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
	$(call QTOOL,python3 $(QROOT)/00_led_blinking/scripts/check_timing_slacks.py \
	  $(REVISION_NAME).sta.rpt 2>/dev/null || true)

program-sof: usb-wsl $(REVISION_NAME).sof
	$(QUARTUS_PROGRAM)

# download-elf / terminal need the uname shim and --privileged / -it mounts
# that QTOOL does not provide; they are written as raw docker run commands.
download-elf: usb-wsl $(APP_DIR)/nios2_led.elf
	docker run --rm --user $$(id -u):$$(id -g) --privileged \
	  -v $(REPO_ROOT):/work \
	  -v $(UNAME_SHIM):/usr/local/bin/uname:ro \
	  $(DOCKER_IMAGE) \
	  nios2-download -g /work/$(PROJECT_NAME)/software/app/nios2_led.elf

terminal: usb-wsl
	docker run --rm --user $$(id -u):$$(id -g) -it --privileged \
	  -v $(UNAME_SHIM):/usr/local/bin/uname:ro \
	  $(DOCKER_IMAGE) \
	  nios2-terminal

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
