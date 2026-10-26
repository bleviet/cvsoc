#==============================================================================
# DE10-Nano pin assignments for the 18_gige_camera project
#
# Port names match the VHDL top-level wrapper "de10_nano_top".
#
# Notes:
#   - HPS DDR3 pins are assigned automatically by Quartus when the altera_hps
#     component is instantiated; no explicit set_location_assignment is needed.
#   - Only FPGA fabric pins are assigned here.
#==============================================================================

#============================================================
# FPGA clock (50 MHz oscillator)
#============================================================
set_instance_assignment -name IO_STANDARD "3.3-V LVTTL" -to fpga_clk1_50
set_location_assignment PIN_V11 -to fpga_clk1_50

#============================================================
# Push-button KEY[0] (active-LOW)
#============================================================
set_instance_assignment -name IO_STANDARD "3.3-V LVTTL" -to key[0]
set_location_assignment PIN_AH17 -to key[0]

#============================================================
# Board LEDs (active-LOW on DE10-Nano)
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

#============================================================
# TRDB-D5M on GPIO header JP1 (GPIO_0)
#
# Header pin -> D5M signal from the TRDB-D5M User Manual (Figure 2-1);
# header pin -> FPGA pin from the DE10-Nano User Manual (Figure 3-20) and the
# System CD's DE10_Nano_golden_top.qsf.
#
#   JP1 pin  GPIO_0  FPGA    D5M        JP1 pin  GPIO_0  FPGA    D5M
#   1        [0]     V12     PIXCLK     2        [1]     E8      D11
#   4        [3]     D11     D10        5        [4]     D8      D9
#   6        [5]     AH13    D8         7        [6]     AF7     D7
#   8        [7]     AH14    D6         9        [8]     AF4     D5
#   10       [9]     AH3     D4         13       [10]    AD5     D3
#   14       [11]    AG14    D2         15       [12]    AE23    D1
#   16       [13]    AE6     D0         19       [16]    D12     XCLKIN
#   20       [17]    AD20    RESETn     22       [19]    AD17    TRIGGER
#   23       [20]    AC23    STROBE     24       [21]    AC22    LVAL
#   25       [22]    Y19     FVAL       26       [23]    AB23    SDATA
#   27       [24]    AA19    SCLK       29       -       -       3.3 V
#============================================================
set d5m_pins {
  d5m_pixclk  V12
  d5m_d[11]   E8
  d5m_d[10]   D11
  d5m_d[9]    D8
  d5m_d[8]    AH13
  d5m_d[7]    AF7
  d5m_d[6]    AH14
  d5m_d[5]    AF4
  d5m_d[4]    AH3
  d5m_d[3]    AD5
  d5m_d[2]    AG14
  d5m_d[1]    AE23
  d5m_d[0]    AE6
  d5m_xclkin  D12
  d5m_reset_n AD20
  d5m_trigger AD17
  d5m_strobe  AC23
  d5m_lval    AC22
  d5m_fval    Y19
  d5m_sdata   AB23
  d5m_sclk    AA19
}
foreach {port pin} $d5m_pins {
  set_instance_assignment -name IO_STANDARD "3.3-V LVTTL" -to $port
  set_location_assignment PIN_$pin -to $port
}

# Sample the pixel bus in the I/O cells: equal, short paths for all bits.
foreach port {d5m_d[*] d5m_fval d5m_lval} {
  set_instance_assignment -name FAST_INPUT_REGISTER ON -to $port
}

# I2C is open drain; the weak pull-ups back up the pull-ups on the D5M.
set_instance_assignment -name WEAK_PULL_UP_RESISTOR ON -to d5m_sclk
set_instance_assignment -name WEAK_PULL_UP_RESISTOR ON -to d5m_sdata
