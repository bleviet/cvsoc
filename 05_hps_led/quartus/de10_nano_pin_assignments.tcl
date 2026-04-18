#==============================================================================
# DE10-Nano pin assignments for the 05_hps_led project
#
# Port names match the Platform Designer system "hps_system" — the top-level
# entity.  The exported clk, reset, and led_external_connection interfaces map
# directly to FPGA pins; the HPS DDR3 pins are constrained automatically.
#
# Notes:
#   - HPS DDR3 pins are assigned automatically by Quartus when the altera_hps
#     component is instantiated; no explicit set_location_assignment is needed
#     (or permitted) for those pins.
#   - Only FPGA fabric pins and the OCT RZQ input are assigned here.
#==============================================================================

#============================================================
# FPGA clock (50 MHz oscillator) → hps_system clk_clk
#============================================================
set_instance_assignment -name IO_STANDARD "3.3-V LVTTL" -to clk_clk
set_location_assignment PIN_V11 -to clk_clk

#============================================================
# Push-button KEY[0] (active-LOW) → hps_system reset_reset_n
#============================================================
set_instance_assignment -name IO_STANDARD "3.3-V LVTTL" -to reset_reset_n
set_location_assignment PIN_AH17 -to reset_reset_n

#============================================================
# Board LEDs (active-LOW on DE10-Nano) → hps_system led_external_connection_export
#============================================================
set_instance_assignment -name IO_STANDARD "3.3-V LVTTL" -to led_external_connection_export[0]
set_instance_assignment -name IO_STANDARD "3.3-V LVTTL" -to led_external_connection_export[1]
set_instance_assignment -name IO_STANDARD "3.3-V LVTTL" -to led_external_connection_export[2]
set_instance_assignment -name IO_STANDARD "3.3-V LVTTL" -to led_external_connection_export[3]
set_instance_assignment -name IO_STANDARD "3.3-V LVTTL" -to led_external_connection_export[4]
set_instance_assignment -name IO_STANDARD "3.3-V LVTTL" -to led_external_connection_export[5]
set_instance_assignment -name IO_STANDARD "3.3-V LVTTL" -to led_external_connection_export[6]
set_instance_assignment -name IO_STANDARD "3.3-V LVTTL" -to led_external_connection_export[7]

set_location_assignment PIN_W15  -to led_external_connection_export[0]
set_location_assignment PIN_AA24 -to led_external_connection_export[1]
set_location_assignment PIN_V16  -to led_external_connection_export[2]
set_location_assignment PIN_V15  -to led_external_connection_export[3]
set_location_assignment PIN_AF26 -to led_external_connection_export[4]
set_location_assignment PIN_AE26 -to led_external_connection_export[5]
set_location_assignment PIN_Y16  -to led_external_connection_export[6]
set_location_assignment PIN_AA23 -to led_external_connection_export[7]


