# Create the project and overwrite any settings files that exist
project_new 05_hps_led -revision de10_nano -overwrite

# ── Device ───────────────────────────────────────────────────────────────────
set_global_assignment -name FAMILY "Cyclone V"
set_global_assignment -name DEVICE 5CSEBA6U23I7

# ── Timing constraints ────────────────────────────────────────────────────────
set_global_assignment -name SDC_FILE de10_nano.sdc

# ── Top-level entity ──────────────────────────────────────────────────────────
# Use hps_system directly as the FPGA top-level.  Using the generated QIP
# (not QSYS_FILE) prevents Quartus from extracting unpatched DDR3 soft-model
# files from its installation into db/ip/, which would cause Error 174068.
set_global_assignment -name TOP_LEVEL_ENTITY hps_system

# ── Source files ──────────────────────────────────────────────────────────────
# Generated QIP: provides hps_system.v and all submodule HDL files (patched).
# Do NOT add QSYS_FILE here — Quartus would re-extract unpatched IP to db/ip/.
set_global_assignment -name QIP_FILE   ../qsys/hps_system/synthesis/hps_system.qip

# ── Pin assignments ───────────────────────────────────────────────────────────
source de10_nano_pin_assignments.tcl

project_close
