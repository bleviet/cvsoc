package require -exact qsys 12.0

# ---------------------------------------------------------------------------
# 18_gige_camera system for DE10-Nano (Cyclone V 5CSEBA6U23I7)
#
# Components:
#   - HPS Cortex-A9 with DDR3-800 SDRAM controller (settings from 15_ddr3_fpga_hps)
#   - Lightweight HPS-to-FPGA AXI bridge (2 MB window at 0xFF200000)
#   - FPGA-to-HPS AXI bridge (64-bit slave): FPGA masters see the HPS address
#     map, so DDR is at its physical address (frame buffers at 0x3C000000)
#   - cam: cam_frame_writer, D5M capture + test-pattern generator + DDR
#     frame writer; its d5m conduit is exported to the top level
#   - hdmi: hdmi_preview, shows cam's latest frame on the ADV7513 (720p60);
#     reads DDR through the same FPGA-to-HPS bridge
#   - video_pll: 74.25 MHz pixel clock for hdmi
#
# Frames go through the FPGA-to-HPS AXI bridge rather than the FPGA-to-SDRAM
# ports: the SDRAM ports are configured by the preloader and cannot be safely
# reconfigured once Linux runs from SDRAM (ADR-008).
#
# ARM address map (LW bridge window):
#   0xFF200000  cam_frame_writer  CSRs (see hdl/cam_frame_writer.vhd)
#   0xFF200100  hdmi_preview      CSRs (see hdl/hdmi_preview.vhd)
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

# Disable the full HPS-to-FPGA bridge (not needed in this design)
set_instance_parameter_value hps_0 S2F_Width             {0}

# FPGA-to-HPS AXI bridge, 64-bit (encoding: 0=disabled, 1=32, 2=64, 3=128)
set_instance_parameter_value hps_0 F2S_Width             {2}

# No FPGA-to-SDRAM ports (see header)
set_instance_parameter_value hps_0 F2SDRAM_Type          {}
set_instance_parameter_value hps_0 F2SDRAM_Width         {}

# Disable MPU standby/event conduit: not used in this design.
set_instance_parameter_value hps_0 MPU_EVENTS_Enable     {false}

# ── FPGA fabric clock (50 MHz from FPGA_CLK1_50 top-level port) ──────────
add_instance clk_0 clock_source
set_instance_parameter_value clk_0 clockFrequency      {50000000}
set_instance_parameter_value clk_0 clockFrequencyKnown {1}
set_instance_parameter_value clk_0 resetSynchronousEdges {NONE}

# ── Camera frame writer (custom component) ────────────────────────────────
add_instance cam cam_frame_writer
set_instance_parameter_value cam SENSOR_WIDTH {2592}

# ── HDMI preview and its pixel clock ──────────────────────────────────────
# 720p60 needs 74.25 MHz = 50 MHz x 1.485, so the PLL runs fractional-N.
add_instance video_pll altera_pll
set_instance_parameter_value video_pll gui_reference_clock_frequency {50.0}
set_instance_parameter_value video_pll gui_pll_mode {Fractional-N PLL}
set_instance_parameter_value video_pll gui_number_of_clocks {1}
set_instance_parameter_value video_pll gui_output_clock_frequency0 {74.25}
set_instance_parameter_value video_pll gui_use_locked {false}

add_instance hdmi hdmi_preview

# ── Clock connections ──────────────────────────────────────────────────────
add_connection clk_0.clk       cam.clk
add_connection clk_0.clk       hps_0.h2f_lw_axi_clock
add_connection clk_0.clk       hps_0.f2h_axi_clock
add_connection clk_0.clk       video_pll.refclk
add_connection clk_0.clk       hdmi.clk
add_connection video_pll.outclk0 hdmi.pclk

# ── Reset connections ──────────────────────────────────────────────────────
add_connection hps_0.h2f_reset cam.reset
add_connection hps_0.h2f_reset hdmi.reset
add_connection clk_0.clk_reset video_pll.reset

# ── LW bridge: camera CSRs at 0xFF200000 ───────────────────────────────────
add_connection hps_0.h2f_lw_axi_master cam.avs
set_connection_parameter_value hps_0.h2f_lw_axi_master/cam.avs baseAddress {0x00000000}
add_connection hps_0.h2f_lw_axi_master hdmi.avs
set_connection_parameter_value hps_0.h2f_lw_axi_master/hdmi.avs baseAddress {0x00000100}

# ── FPGA-to-HPS bridge: frame writes ───────────────────────────────────────
add_connection cam.avm hps_0.f2h_axi_slave
set_connection_parameter_value cam.avm/hps_0.f2h_axi_slave baseAddress {0x00000000}
add_connection hdmi.avm hps_0.f2h_axi_slave
set_connection_parameter_value hdmi.avm/hps_0.f2h_axi_slave baseAddress {0x00000000}

# ── Last completed frame: writer -> HDMI preview ───────────────────────────
add_connection cam.frame hdmi.frame

# ── Interface exports ──────────────────────────────────────────────────────
add_interface clk clock sink
set_interface_property clk EXPORT_OF clk_0.clk_in

add_interface reset reset sink
set_interface_property reset EXPORT_OF clk_0.clk_in_reset

add_interface memory memory master
set_interface_property memory EXPORT_OF hps_0.memory

add_interface d5m conduit end
set_interface_property d5m EXPORT_OF cam.d5m

add_interface hdmi conduit end
set_interface_property hdmi EXPORT_OF hdmi.hdmi

save_system hps_system.qsys
puts "hps_system.qsys saved successfully"
