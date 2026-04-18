# Makefile for the 05_hps_led Quartus project.
# Orchestrates: Platform Designer generation → Quartus compile → ARM app build.
#
# All steps run inside the raetro/quartus:23.1 Docker container.
# Usage (from repo root):
#   docker run --rm -v $(pwd):/work raetro/quartus:23.1 \
#     bash -c "cd /work/05_hps_led/quartus && make setup && make all"
#
# The 'setup' target installs the ARM cross-compiler from the Debian snapshot
# archive.  It is only needed once per fresh container session.

PROJECT_NAME  = 05_hps_led
REVISION_NAME = de10_nano

QSYS_TCL      = ../qsys/hps_system.tcl
QSYS_FILE     = ../qsys/hps_system.qsys
QSYS_GEN_DIR  = ../qsys/hps_system/synthesis

APP_DIR       = ../software/app

ARM_CC        ?= arm-linux-gnueabihf-gcc

.PHONY: all setup qsys patch-oct project compile app clean check_timing

all: qsys project compile app

# ── Step 0: install ARM cross-compiler (Debian 9 snapshot) ───────────────
# The raetro/quartus:23.1 container is Debian 9 (stretch); the package is
# available from the snapshot archive.
setup:
	@echo "Installing ARM cross-compiler (arm-linux-gnueabihf-gcc)…"
	echo "deb http://snapshot.debian.org/archive/debian/20220622T000000Z stretch main" \
	     > /etc/apt/sources.list.d/snapshot.list
	apt-get update -o Acquire::Check-Valid-Until=false -qq
	apt-get install -y -o Acquire::Check-Valid-Until=false \
	     gcc-arm-linux-gnueabihf binutils-arm-linux-gnueabihf
	@echo "ARM cross-compiler installed: $$(arm-linux-gnueabihf-gcc --version | head -1)"

# ── Step 1: generate the .qsys file from the TCL script ──────────────────
$(QSYS_FILE): $(QSYS_TCL)
	cd ../qsys && qsys-script --script=hps_system.tcl

qsys: $(QSYS_FILE)
	qsys-generate $(QSYS_FILE) --synthesis=VERILOG
	@$(MAKE) patch-oct

# ── Patch generated DDR3 PHY to propagate USE_TERMINATION_CONTROL ────────
# qsys-generate produces altdq_dqs2_acv_connect_to_hard_phy_cyclonev.sv
# with USE_TERMINATION_CONTROL declared but never forwarded to the
# cyclonev_io_obuf primitives, causing Fitter Error 174068 when the
# SERIESTERMINATIONCONTROL port is connected. Add the missing parameter.
patch-oct:
	@echo "Patching DDR3 PHY: propagating USE_TERMINATION_CONTROL to I/O buffers…"
	@python3 ../../05_hps_led/scripts/patch_oct.py \
	    ../qsys/hps_system/synthesis/submodules/altdq_dqs2_acv_connect_to_hard_phy_cyclonev.sv

# ── Step 2: create and compile the Quartus project ────────────────────────
project: $(QSYS_GEN_DIR)/hps_system.qip
	quartus_sh -t de10_nano_project.tcl

# Two-pass compilation flow required for Cyclone V HPS DDR3:
#
#  Pass 1  quartus_map   – synthesise the design and build a timing netlist
#  Apply   quartus_sta   – run hps_sdram_p0_pin_assignments.tcl to inject the
#                          DDR3 I/O-standard and OCT-calibration assignments
#                          into the QSF (needs the timing netlist from pass 1)
#  Pass 2  quartus_map   – re-synthesise picking up the new QSF assignments
#          quartus_fit   – place & route (DDR3 OCT errors resolved by pass 1+apply)
#          quartus_asm   – generate the .sof bitstream
#          quartus_sta   – final timing analysis
compile:
	quartus_map  --read_settings_files=on --write_settings_files=off \
	             $(PROJECT_NAME) -c $(REVISION_NAME)
	quartus_sta  -t $(QSYS_GEN_DIR)/submodules/hps_sdram_p0_pin_assignments.tcl \
	             $(REVISION_NAME)
	quartus_map  --read_settings_files=on --write_settings_files=off \
	             $(PROJECT_NAME) -c $(REVISION_NAME)
	quartus_fit  --read_settings_files=on --write_settings_files=off \
	             $(PROJECT_NAME) -c $(REVISION_NAME)
	quartus_asm  --read_settings_files=on --write_settings_files=off \
	             $(PROJECT_NAME) -c $(REVISION_NAME)
	quartus_sta  $(PROJECT_NAME) -c $(REVISION_NAME)

# ── Step 3: cross-compile the ARM bare-metal application ─────────────────
app:
	$(MAKE) -C $(APP_DIR) CC=$(ARM_CC)

# ── Timing check ──────────────────────────────────────────────────────────
check_timing:
	python ../../00_led_blinking/scripts/check_timing_slacks.py \
	       $(REVISION_NAME).sta.rpt 2>/dev/null || true

# ── Clean ─────────────────────────────────────────────────────────────────
FILE_ENDINGS_FOR_REMOVAL = .rpt .summary .qsf .qpf .qws .done .smsg .jdi .pin .sld .sof dump.txt

clean:
	rm -rf db incremental_db output_files hps_isw_handoff
	rm -rf $(QSYS_FILE) ../qsys/hps_system ../qsys/hps_system.sopcinfo
	$(MAKE) -C $(APP_DIR) clean 2>/dev/null || true
	rm -f $(foreach ENDING,$(FILE_ENDINGS_FOR_REMOVAL),$(wildcard *$(ENDING)))
