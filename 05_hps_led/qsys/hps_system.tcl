package require -exact qsys 12.0

# ---------------------------------------------------------------------------
# HPS LED demo system for DE10-Nano (Cyclone V 5CSEBA6U23I7)
#
# Components:
#   - HPS Cortex-A9 with DDR3-800 SDRAM controller
#   - Lightweight HPS-to-FPGA AXI bridge
#   - LED PIO (8-bit output) on the lightweight bridge
#
# From the ARM Cortex-A9, the LED PIO register is at:
#   H2F_LW_BASE + 0x00000000 = 0xFF200000
# ---------------------------------------------------------------------------

create_system hps_system
set_project_property DEVICE_FAMILY {Cyclone V}
set_project_property DEVICE {5CSEBA6U23I7}

# ── HPS (Hard Processor System) ───────────────────────────────────────────
add_instance hps_0 altera_hps

# DDR3 protocol — DE10-Nano uses two ×16 DDR3 chips (32-bit bus, 800 MT/s)
set_instance_parameter_value hps_0 HPS_PROTOCOL          {DDR3}
set_instance_parameter_value hps_0 MEM_VENDOR            {Micron}
set_instance_parameter_value hps_0 MEM_FORMAT            {DISCRETE}

# DDR3-800: clock = 400 MHz (= 800 MT/s / 2)
set_instance_parameter_value hps_0 MEM_CLK_FREQ          {400.0}
set_instance_parameter_value hps_0 MEM_CLK_FREQ_MAX      {400.0}
set_instance_parameter_value hps_0 MEM_VOLTAGE           {1.35V DDR3L}

# Memory geometry: 2 × MT41K256M16HA gives 32-bit bus, 15-row × 10-col × 8-bank
set_instance_parameter_value hps_0 MEM_DQ_WIDTH          {32}
set_instance_parameter_value hps_0 MEM_DQ_PER_DQS        {8}
set_instance_parameter_value hps_0 MEM_ROW_ADDR_WIDTH    {15}
set_instance_parameter_value hps_0 MEM_COL_ADDR_WIDTH    {10}
set_instance_parameter_value hps_0 MEM_BANKADDR_WIDTH    {3}
set_instance_parameter_value hps_0 MEM_IF_DM_PINS_EN     {1}
set_instance_parameter_value hps_0 MEM_NUMBER_OF_RANKS_PER_DIMM  {1}
set_instance_parameter_value hps_0 MEM_CK_WIDTH          {1}

# DDR3L timing for MT41K256M16HA-107 @ 400 MHz
set_instance_parameter_value hps_0 TIMING_TIS            {175}
set_instance_parameter_value hps_0 TIMING_TIH            {250}
set_instance_parameter_value hps_0 TIMING_TDS            {50}
set_instance_parameter_value hps_0 TIMING_TDH            {125}
set_instance_parameter_value hps_0 TIMING_TDQSQ          {200}
set_instance_parameter_value hps_0 TIMING_TQH            {0.38}
set_instance_parameter_value hps_0 TIMING_TQSH           {0.38}
set_instance_parameter_value hps_0 TIMING_TDQSCK         {350}
set_instance_parameter_value hps_0 TIMING_TDQSS          {0.25}
set_instance_parameter_value hps_0 TIMING_TDSH           {0.2}
set_instance_parameter_value hps_0 TIMING_TDSS           {0.2}
set_instance_parameter_value hps_0 MEM_TINIT_US          {500}
set_instance_parameter_value hps_0 MEM_TMRD_CK           {4}
set_instance_parameter_value hps_0 MEM_TRAS_NS           {35.0}
set_instance_parameter_value hps_0 MEM_TRCD_NS           {13.75}
set_instance_parameter_value hps_0 MEM_TRP_NS            {13.75}
set_instance_parameter_value hps_0 MEM_TREFI_US          {7.8}
set_instance_parameter_value hps_0 MEM_TRFC_NS           {260.0}
set_instance_parameter_value hps_0 MEM_TWR_NS            {15.0}
set_instance_parameter_value hps_0 MEM_TWTR              {4}
set_instance_parameter_value hps_0 MEM_TFAW_NS           {40.0}
set_instance_parameter_value hps_0 MEM_TRRD_NS           {7.5}
set_instance_parameter_value hps_0 MEM_TRTP_NS           {7.5}

# DDR3 ODT (On-Die Termination) — enables HPS OCT calibration.
# Rtt_Nom = RZQ/6 (40Ω) is standard for single-rank DDR3 on DE10-Nano.
# Without this, altera_hps generates SERIESTERMINATIONCONTROL-connected
# output buffers whose USE_TERMINATION_CONTROL is "false" → Quartus error 174068.
set_instance_parameter_value hps_0 MEM_RTT_NOM           {RZQ/6}
set_instance_parameter_value hps_0 MEM_RTT_WR            {Dynamic ODT off}

# Enable the Lightweight HPS-to-FPGA AXI bridge (32-bit, 2 MB window)
# This bridge is used for low-bandwidth register access (e.g., LED PIO).
set_instance_parameter_value hps_0 LWH2F_Enable          {true}

# Disable the full HPS-to-FPGA and FPGA-to-HPS heavy AXI bridges (not needed)
set_instance_parameter_value hps_0 F2S_Width             {0}
set_instance_parameter_value hps_0 S2F_Width             {0}

# Disable MPU standby/event conduit: not used in this design.
# When enabled (default=true) Quartus 23.1 altera_hps generates a Warning
# "hps_0.h2f_mpu_events must be exported, or connected to a matching conduit."
# because the conduit is left dangling.  Setting it false suppresses that warning.
set_instance_parameter_value hps_0 MPU_EVENTS_Enable     {false}

# ── FPGA fabric clock (50 MHz from FPGA_CLK1_50 top-level port) ──────────
# This clock_source component represents an external clock input to the
# Platform Designer system.  The VHDL top-level connects FPGA_CLK1_50 here.
add_instance clk_0 clock_source
set_instance_parameter_value clk_0 clockFrequency      {50000000}
set_instance_parameter_value clk_0 clockFrequencyKnown {1}
set_instance_parameter_value clk_0 resetSynchronousEdges {NONE}

# ── LED PIO (8-bit output register on the LW bridge) ──────────────────────
add_instance led_pio altera_avalon_pio
set_instance_parameter_value led_pio width       {8}
set_instance_parameter_value led_pio direction   {Output}
set_instance_parameter_value led_pio resetValue  {0}

# Clock the LED PIO from the FPGA fabric clock
add_connection clk_0.clk       led_pio.clk
# Drive the HPS LW bridge clock from the same FPGA fabric clock source.
# This puts h2f_lw_axi_master and led_pio.s1 in the SAME clock domain,
# which avoids Platform Designer's clock-crossing interconnect insertion
# (which fails with EntityWritingException for the Cyclone V hard HPS).
add_connection clk_0.clk       hps_0.h2f_lw_axi_clock
# Supply the F2H SDRAM clock (required by HPS even with F2H SDRAM unused)
add_connection clk_0.clk       hps_0.f2h_sdram0_clock
# Reset the LED PIO from the HPS h2f_reset output; the LED PIO is held in
# reset until the HPS boot firmware releases the FPGA reset.
add_connection hps_0.h2f_reset led_pio.reset

# Connect the LED PIO as a slave on the Lightweight HPS-to-FPGA bridge
add_connection hps_0.h2f_lw_axi_master led_pio.s1

# LED PIO base address within the LW bridge address window:
#   ARM address = 0xFF200000 + 0x0000 = 0xFF200000
set_connection_parameter_value hps_0.h2f_lw_axi_master/led_pio.s1 baseAddress {0x00000000}

# ── Interface exports ──────────────────────────────────────────────────────
# FPGA fabric clock input (50 MHz from board oscillator)
add_interface clk clock sink
set_interface_property clk EXPORT_OF clk_0.clk_in

# FPGA fabric reset (active-low, from top-level push-button KEY[0])
add_interface reset reset sink
set_interface_property reset EXPORT_OF clk_0.clk_in_reset

# Export the h2f_lw_axi_clock so Quartus can use it / close timing on it
# NOTE: h2f_lw_axi_clock is now driven internally by clk_0 (same domain as
# led_pio), so there is no longer anything to export here.

# DDR3 memory pins → go directly to physical HPS memory I/O pads
add_interface memory memory master
set_interface_property memory EXPORT_OF hps_0.memory

# LED PIO output → drives the 8 on-board LEDs through FPGA I/O
add_interface led_external_connection conduit end
set_interface_property led_external_connection EXPORT_OF led_pio.external_connection

save_system hps_system.qsys
puts "hps_system.qsys saved successfully"
