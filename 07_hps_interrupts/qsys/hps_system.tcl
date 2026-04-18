package require -exact qsys 12.0

# ---------------------------------------------------------------------------
# HPS interrupt demo system for DE10-Nano (Cyclone V 5CSEBA6U23I7)
#
# Extends 05_hps_led with a 2-bit button PIO connected to the Lightweight
# HPS-to-FPGA bridge.  The button PIO IRQ is wired to the HPS Generic
# Interrupt Controller (GIC) via the FPGA-to-HPS IRQ interface.
#
# Components:
#   - HPS Cortex-A9 with DDR3-800 SDRAM controller
#   - Lightweight HPS-to-FPGA AXI bridge (2 MB window at 0xFF200000)
#   - LED PIO (8-bit output)     at LW_BASE + 0x0000 = 0xFF200000
#   - Button PIO (2-bit input,   at LW_BASE + 0x1000 = 0xFF201000
#     falling-edge IRQ → GIC interrupt 72)
#
# ARM address map (LW bridge window):
#   0xFF200000  led_pio     DATA register (write LED pattern)
#   0xFF201000  button_pio  DATA register (read button state)
#   0xFF201008  button_pio  IRQ_MASK register (write 0x3 to arm both buttons)
#   0xFF20100C  button_pio  EDGE_CAPTURE register (read = which buttons pressed,
#                            write any value = clear captured edges)
#
# FPGA-to-HPS interrupt routing:
#   button_pio.irq → hps_0.f2h_irq0 (irqNumber 0) → GIC interrupt ID 72
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

# DDR3 ODT (On-Die Termination)
set_instance_parameter_value hps_0 MEM_RTT_NOM           {RZQ/6}
set_instance_parameter_value hps_0 MEM_RTT_WR            {Dynamic ODT off}

# Enable the Lightweight HPS-to-FPGA AXI bridge (32-bit, 2 MB window)
set_instance_parameter_value hps_0 LWH2F_Enable          {true}

# Disable the full HPS-to-FPGA and FPGA-to-HPS heavy AXI bridges
set_instance_parameter_value hps_0 F2S_Width             {0}
set_instance_parameter_value hps_0 S2F_Width             {0}

# ── FPGA fabric clock (50 MHz from FPGA_CLK1_50 top-level port) ──────────
add_instance clk_0 clock_source
set_instance_parameter_value clk_0 clockFrequency      {50000000}
set_instance_parameter_value clk_0 clockFrequencyKnown {1}
set_instance_parameter_value clk_0 resetSynchronousEdges {NONE}

# ── LED PIO (8-bit output register on the LW bridge) ──────────────────────
add_instance led_pio altera_avalon_pio
set_instance_parameter_value led_pio width       {8}
set_instance_parameter_value led_pio direction   {Output}
set_instance_parameter_value led_pio resetValue  {0}

# ── Button PIO (2-bit input, falling-edge IRQ on the LW bridge) ───────────
# KEY[0] and KEY[1] are active-LOW: falling edge = button pressed.
# The edge-capture register holds which buttons have been pressed since the
# last clear.  The IRQ is asserted while any captured edge matches the mask.
add_instance button_pio altera_avalon_pio
set_instance_parameter_value button_pio width        {2}
set_instance_parameter_value button_pio direction    {Input}
set_instance_parameter_value button_pio resetValue   {0}
set_instance_parameter_value button_pio captureEdge  {1}
set_instance_parameter_value button_pio edgeType     {FALLING}
set_instance_parameter_value button_pio generateIRQ  {1}
set_instance_parameter_value button_pio irqType      {EDGE}

# ── Clock connections ──────────────────────────────────────────────────────
add_connection clk_0.clk       led_pio.clk
add_connection clk_0.clk       button_pio.clk
add_connection clk_0.clk       hps_0.h2f_lw_axi_clock
add_connection clk_0.clk       hps_0.f2h_sdram0_clock

# ── Reset connections ──────────────────────────────────────────────────────
# The FPGA peripherals are held in reset until the HPS boot firmware releases
# the FPGA reset (h2f_rst_n assertion → de-assertion after JTAG programming).
add_connection hps_0.h2f_reset led_pio.reset
add_connection hps_0.h2f_reset button_pio.reset

# The FPGA clock source is also reset by the HPS, ensuring the fabric clock
# domain starts cleanly after every JTAG programming cycle.
add_connection hps_0.h2f_reset clk_0.clk_in_reset

# ── Avalon-MM bus connections ──────────────────────────────────────────────
add_connection hps_0.h2f_lw_axi_master led_pio.s1
add_connection hps_0.h2f_lw_axi_master button_pio.s1

# Address map within the 2 MB LW bridge window:
#   ARM address = 0xFF200000 + baseAddress
set_connection_parameter_value hps_0.h2f_lw_axi_master/led_pio.s1    baseAddress {0x00000000}
set_connection_parameter_value hps_0.h2f_lw_axi_master/button_pio.s1 baseAddress {0x00001000}

# ── FPGA-to-HPS interrupt connection ──────────────────────────────────────
# button_pio IRQ → hps_0.f2h_irq0 (irqNumber 0)
# In the GIC: F2H_IRQ[0] → SPI[40] → GIC interrupt ID 72
add_connection button_pio.irq hps_0.f2h_irq0
set_connection_parameter_value button_pio.irq/hps_0.f2h_irq0 irqNumber {0}

# ── Interface exports ──────────────────────────────────────────────────────
# FPGA fabric clock input (50 MHz from board oscillator)
add_interface clk clock sink
set_interface_property clk EXPORT_OF clk_0.clk_in

# DDR3 memory pins → directly to HPS physical memory I/O pads
add_interface memory memory master
set_interface_property memory EXPORT_OF hps_0.memory

# LED PIO output → drives the 8 on-board LEDs
add_interface led_external_connection conduit end
set_interface_property led_external_connection EXPORT_OF led_pio.external_connection

# Button PIO input → wired to KEY[0] and KEY[1] on the DE10-Nano board
add_interface button_external_connection conduit end
set_interface_property button_external_connection EXPORT_OF button_pio.external_connection

save_system hps_system.qsys
puts "hps_system.qsys saved successfully"
