# Makefile for the 14_ddr3_hps_test Quartus project.
# Orchestrates: Platform Designer generation → Quartus compile → ARM app build.
#
# All steps run inside the cvsoc/quartus:23.1 Docker container.

PROJECT_NAME  = 14_ddr3_hps_test
REVISION_NAME = de10_nano

QSYS_TCL      = ../qsys/hps_system.tcl
QSYS_FILE     = ../qsys/hps_system.qsys
QSYS_GEN_DIR  = ../qsys/hps_system/synthesis

APP_DIR       = ../software/app

ARM_CC        ?= arm-linux-gnueabihf-gcc

DOCKER_IMAGE ?= cvsoc/quartus:23.1

# usbipd-win bus ID for the USB-Blaster II.
USBIPD       ?= usbipd.exe
USBIPD_BUSID ?= 2-4

# JTAG chain device index: 1=HPS ARM DAP, 2=Cyclone V FPGA
DEVICE_INDEX ?= 2

REPO_ROOT    = $(realpath ../..)
UNAME_SHIM   = $(REPO_ROOT)/common/docker/uname_shim.sh
DOCKER       = docker run --rm --user $$(id -u):$$(id -g) -v $(REPO_ROOT):/work $(DOCKER_IMAGE) bash -c
DOCKER_CWD   = cd /work/$(PROJECT_NAME)/quartus
DOCKER_GUI   = docker run --rm --user $$(id -u):$$(id -g) \
  -e DISPLAY=$$DISPLAY \
  -v /tmp/.X11-unix:/tmp/.X11-unix \
  -v /mnt/wslg:/mnt/wslg \
  -e WAYLAND_DISPLAY=$$WAYLAND_DISPLAY \
  -e XDG_RUNTIME_DIR=/mnt/wslg/runtime-dir \
  -v $(REPO_ROOT):/work \
  $(DOCKER_IMAGE) bash -c

# HPS board connection for deploy-elf.
HPS_IP       ?= 192.168.1.100
HPS_USER     ?= root

.PHONY: all setup qsys patch-oct project compile app clean check_timing \
        program-sof download-elf deploy-elf usb-windows usb-wsl \
        quartus-gui platform-designer

# ── GUI targets ───────────────────────────────────────────────────────────────
# Opens a GUI tool inside Docker with X11 forwarding to the WSL2 host.
# Requires WSLg (Windows 11) or a running X server (DISPLAY must be set).

# quartus-gui: Opens Quartus Prime with the project loaded.
#   Run 'make project' first if de10_nano.qpf does not yet exist.
quartus-gui:
	$(DOCKER_GUI) "$(DOCKER_CWD) && quartus $(PROJECT_NAME).qpf 2>/dev/null || quartus"

# platform-designer: Opens Platform Designer with the .qsys system file.
#   Run 'make $(QSYS_FILE)' first if the .qsys file does not yet exist.
platform-designer:
	$(DOCKER_GUI) "$(DOCKER_CWD) && qsys-edit $(QSYS_FILE) 2>/dev/null || qsys-edit"

all:
	docker run --rm --user $$(id -u):$$(id -g) -v $(REPO_ROOT):/work $(DOCKER_IMAGE) bash -c '\
	  cd /work/$(PROJECT_NAME)/qsys && qsys-script --script=hps_system.tcl && \
	  cd /work/$(PROJECT_NAME)/quartus && \
	  qsys-generate ../qsys/hps_system.qsys --synthesis=VERILOG && \
	  python3 ../../05_hps_led/scripts/patch_oct.py \
	    ../qsys/hps_system/synthesis/submodules/altdq_dqs2_acv_connect_to_hard_phy_cyclonev.sv && \
	  quartus_sh -t de10_nano_project.tcl && \
	  quartus_map --read_settings_files=on --write_settings_files=off $(PROJECT_NAME) -c $(REVISION_NAME) && \
	  quartus_sta -t ../qsys/hps_system/synthesis/submodules/hps_sdram_p0_pin_assignments.tcl $(REVISION_NAME) && \
	  quartus_map --read_settings_files=on --write_settings_files=off $(PROJECT_NAME) -c $(REVISION_NAME) && \
	  quartus_fit --read_settings_files=on --write_settings_files=off $(PROJECT_NAME) -c $(REVISION_NAME) && \
	  quartus_asm --read_settings_files=on --write_settings_files=off $(PROJECT_NAME) -c $(REVISION_NAME) && \
	  quartus_sta $(PROJECT_NAME) -c $(REVISION_NAME) && \
	  make -C ../software/app CC=arm-linux-gnueabihf-gcc'

# ── Step 1: generate the .qsys file from the TCL script ──────────────────
$(QSYS_FILE): $(QSYS_TCL)
	$(DOCKER) "$(DOCKER_CWD) && cd ../qsys && qsys-script --script=hps_system.tcl"

qsys: $(QSYS_FILE)
	$(DOCKER) "$(DOCKER_CWD) && \
	  qsys-generate $(QSYS_FILE) --synthesis=VERILOG && \
	  python3 ../../05_hps_led/scripts/patch_oct.py \
	    ../qsys/hps_system/synthesis/submodules/altdq_dqs2_acv_connect_to_hard_phy_cyclonev.sv"

# ── Patch generated DDR3 PHY ─────────────────────────────────────────────
patch-oct:
	$(DOCKER) "$(DOCKER_CWD) && \
	  python3 ../../05_hps_led/scripts/patch_oct.py \
	    ../qsys/hps_system/synthesis/submodules/altdq_dqs2_acv_connect_to_hard_phy_cyclonev.sv"

# ── Step 2: create and compile the Quartus project ────────────────────────
project: $(QSYS_GEN_DIR)/hps_system.qip
	$(DOCKER) "$(DOCKER_CWD) && quartus_sh -t de10_nano_project.tcl"

# Two-pass compilation flow required for Cyclone V HPS DDR3:
compile:
	$(DOCKER) "$(DOCKER_CWD) && \
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
	  quartus_sta $(PROJECT_NAME) -c $(REVISION_NAME)"

# ── Step 3: cross-compile the ARM bare-metal application ─────────────────
app:
	$(DOCKER) "$(DOCKER_CWD) && make -C $(APP_DIR) CC=$(ARM_CC)"

# ── Programming & deploy targets ──────────────────────────────────────────────
usb-wsl:
	@echo "Connecting USB-Blaster (busid $(USBIPD_BUSID)) to WSL2..."
	$(USBIPD) attach --wsl --busid $(USBIPD_BUSID) 2>/dev/null || true
	@sleep 1

usb-windows:
	@echo "Detaching USB-Blaster (busid $(USBIPD_BUSID)) from WSL2 → Windows..."
	$(USBIPD) detach --busid $(USBIPD_BUSID) 2>/dev/null || true
	@sleep 1

program-sof: usb-wsl $(REVISION_NAME).sof
	docker run --rm --user $$(id -u):$$(id -g) --privileged \
	  -v /dev/bus/usb:/dev/bus/usb \
	  -v $(REPO_ROOT):/work \
	  $(DOCKER_IMAGE) \
	  bash -c 'jtagd && sleep 2 && \
	    quartus_pgm -m jtag -o "p;/work/$(PROJECT_NAME)/quartus/$(REVISION_NAME).sof@$(DEVICE_INDEX)"; \
	    kill $$(pgrep jtagd) 2>/dev/null || true'

# download-elf: Loads the bare-metal binary into DDR3 via JTAG (OpenOCD).
#   NOTE: The DDR3 controller must be initialised by U-Boot SPL first.
#   Boot the board to U-Boot (do not boot into Linux), then run this target.
#
#   Uses the standard system openocd with the usb_blaster/ublast2 driver.
#
#   Prerequisites on the WSL2 host:
#     sudo apt-get install openocd          # version 0.11.0 or later
#     # udev rules for USB Blaster II are already installed by the repo setup
OPENOCD_UBLAST_CFG = ../scripts/de10_nano_hps_ublast2.cfg
BLASTER_FW         = $(REPO_ROOT)/12_zephyr_led/zephyr/boards/intel/socfpga_std/cyclonev_socdk/support/blaster_6810.hex
APP_BIN            = $(REPO_ROOT)/$(PROJECT_NAME)/software/app/ddr3_hps_test.bin

download-elf: usb-wsl $(APP_DIR)/ddr3_hps_test.elf
	openocd \
	  -c "set BLASTER_FW $(BLASTER_FW)" \
	  -f $(realpath $(OPENOCD_UBLAST_CFG)) \
	  -c "init" \
	  -c "halt" \
	  -c "load_image $(APP_BIN) 0x00100000 bin" \
	  -c "resume 0x00100000" \
	  -c "shutdown"
	@echo "Done: DDR3 HPS test application is running."

deploy-elf: $(APP_DIR)/ddr3_hps_test.elf
	scp $(APP_DIR)/ddr3_hps_test.elf $(APP_DIR)/ddr3_hps_test.bin \
	    $(HPS_USER)@$(HPS_IP):/tmp/
	@echo "ELF + binary copied to $(HPS_USER)@$(HPS_IP):/tmp/"
	@echo "  To run from U-Boot: fatload / go 0x00100000"

# ── Timing check ──────────────────────────────────────────────────────────
check_timing:
	$(DOCKER) "$(DOCKER_CWD) && python /work/00_led_blinking/scripts/check_timing_slacks.py \
	       $(REVISION_NAME).sta.rpt 2>/dev/null || true"

# ── Clean ─────────────────────────────────────────────────────────────────
clean:
	$(DOCKER) "$(DOCKER_CWD) && \
	  rm -rf db incremental_db output_files hps_isw_handoff && \
	  rm -rf $(QSYS_FILE) ../qsys/hps_system ../qsys/hps_system.sopcinfo && \
	  make -C $(APP_DIR) clean 2>/dev/null || true && \
	  find . -maxdepth 1 \( -name '*.rpt' -o -name '*.summary' -o -name '*.qsf' \
	    -o -name '*.qpf' -o -name '*.qws' -o -name '*.done' -o -name '*.smsg' \
	    -o -name '*.jdi' -o -name '*.pin' -o -name '*.sld' -o -name '*.sof' \
	    -o -name 'dump.txt' \) -delete"
