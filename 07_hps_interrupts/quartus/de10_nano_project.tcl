# Create the project and overwrite any settings files that exist
project_new 07_hps_interrupts -revision de10_nano -overwrite

# ── Device ───────────────────────────────────────────────────────────────────
set_global_assignment -name FAMILY "Cyclone V"
set_global_assignment -name DEVICE 5CSEBA6U23I7

# ── Timing constraints ────────────────────────────────────────────────────────
set_global_assignment -name SDC_FILE de10_nano.sdc

# ── Top-level entity ──────────────────────────────────────────────────────────
set_global_assignment -name TOP_LEVEL_ENTITY de10_nano_top

# ── Source files ──────────────────────────────────────────────────────────────
set_global_assignment -name VHDL_FILE        ../hdl/de10_nano_top.vhd
set_global_assignment -name QIP_FILE         ../qsys/hps_system/synthesis/hps_system.qip

# ── Pin assignments ───────────────────────────────────────────────────────────
source de10_nano_pin_assignments.tcl

project_close
