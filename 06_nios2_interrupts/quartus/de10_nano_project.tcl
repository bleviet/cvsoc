# Create the project and overwrite any settings files that exist
project_new 06_nios2_interrupts -revision de10_nano -overwrite

# Device settings
set_global_assignment -name FAMILY "Cyclone V"
set_global_assignment -name DEVICE 5CSEBA6U23I7

set_global_assignment -name VHDL_INPUT_VERSION VHDL_2008

set_global_assignment -name SDC_FILE de10_nano.sdc

# Top-level entity
set_global_assignment -name TOP_LEVEL_ENTITY de10_nano_top

# Source files
set_global_assignment -name VHDL_FILE ../../common/ip/power_on_reset/power_on_reset_generator.vhd
set_global_assignment -name VHDL_FILE ../hdl/de10_nano_top.vhd

# Platform Designer generated system
set_global_assignment -name QIP_FILE ../qsys/nios2_system_gen/synthesis/nios2_system.qip

# Pin assignments
source de10_nano_pin_assignments.tcl

project_close
