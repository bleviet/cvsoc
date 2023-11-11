# Create the project and overwrite any settings files that exist
project_new 01_led_running -revision de10_nano -overwrite

# Set the device and the name of the top-level entity
set_global_assignment -name FAMILY "Cyclone V"
set_global_assignment -name DEVICE 5CSEBA6U23I7

set_global_assignment -name VHDL_INPUT_VERSION VHDL_2008

set_global_assignment -name SDC_FILE de10_nano.sdc

# Add the files to the project
set_global_assignment -name TOP_LEVEL_ENTITY de10_nano_top

set_global_assignment -name VHDL_FILE ../../../common/ip/power_on_reset/power_on_reset_generator.vhd
set_global_assignment -name VHDL_FILE ../hdl/led_running.vhd
set_global_assignment -name VHDL_FILE ../hdl/de10_nano_top.vhd

# Add the pin assignments
source de10_nano_pin_assignments.tcl

project_close
