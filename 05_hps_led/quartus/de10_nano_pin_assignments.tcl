#==============================================================================
# DE10-Nano pin assignments for the 05_hps_led project
#
# Port names match the VHDL top-level wrapper "de10_nano_top".
# The FPGA fabric clock, reset key, and LED ports are named fpga_clk1_50,
# key[0], and led[0..7] respectively.
#
# Notes:
#   - HPS DDR3 pins are assigned automatically by Quartus when the altera_hps
#     component is instantiated; no explicit set_location_assignment is needed
#     (or permitted) for those pins.
#   - Only FPGA fabric pins and the OCT RZQ input are assigned here.
#==============================================================================

#============================================================
# FPGA clock (50 MHz oscillator) → de10_nano_top fpga_clk1_50
#============================================================
set_instance_assignment -name IO_STANDARD "3.3-V LVTTL" -to fpga_clk1_50
set_location_assignment PIN_V11 -to fpga_clk1_50

#============================================================
# Push-button KEY[0] (active-LOW) → de10_nano_top key[0]
#============================================================
set_instance_assignment -name IO_STANDARD "3.3-V LVTTL" -to key[0]
set_location_assignment PIN_AH17 -to key[0]

#============================================================
# Board LEDs (active-LOW on DE10-Nano) → de10_nano_top led[0..7]
#============================================================
set_instance_assignment -name IO_STANDARD "3.3-V LVTTL" -to led[0]
set_instance_assignment -name IO_STANDARD "3.3-V LVTTL" -to led[1]
set_instance_assignment -name IO_STANDARD "3.3-V LVTTL" -to led[2]
set_instance_assignment -name IO_STANDARD "3.3-V LVTTL" -to led[3]
set_instance_assignment -name IO_STANDARD "3.3-V LVTTL" -to led[4]
set_instance_assignment -name IO_STANDARD "3.3-V LVTTL" -to led[5]
set_instance_assignment -name IO_STANDARD "3.3-V LVTTL" -to led[6]
set_instance_assignment -name IO_STANDARD "3.3-V LVTTL" -to led[7]

set_location_assignment PIN_W15  -to led[0]
set_location_assignment PIN_AA24 -to led[1]
set_location_assignment PIN_V16  -to led[2]
set_location_assignment PIN_V15  -to led[3]
set_location_assignment PIN_AF26 -to led[4]
set_location_assignment PIN_AE26 -to led[5]
set_location_assignment PIN_Y16  -to led[6]
set_location_assignment PIN_AA23 -to led[7]

