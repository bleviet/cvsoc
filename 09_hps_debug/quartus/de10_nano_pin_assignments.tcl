#==============================================================================
# DE10-Nano pin assignments for the 07_hps_interrupts project.
#
# Extends 05_hps_led: KEY[0] and KEY[1] are now both button inputs wired to
# the button_pio peripheral (not reset).  The FPGA fabric reset is driven
# internally by the HPS h2f_reset signal.
#==============================================================================

#============================================================
# FPGA clock (50 MHz oscillator) → de10_nano_top fpga_clk1_50
#============================================================
set_instance_assignment -name IO_STANDARD "3.3-V LVTTL" -to fpga_clk1_50
set_location_assignment PIN_V11 -to fpga_clk1_50

#============================================================
# Push-button KEYs (active-LOW) → de10_nano_top key[1:0]
# Both buttons are inputs to the button_pio interrupt peripheral.
#============================================================
set_instance_assignment -name IO_STANDARD "3.3-V LVTTL" -to key[0]
set_instance_assignment -name IO_STANDARD "3.3-V LVTTL" -to key[1]
set_location_assignment PIN_AH17 -to key[0]
set_location_assignment PIN_AH16 -to key[1]

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
