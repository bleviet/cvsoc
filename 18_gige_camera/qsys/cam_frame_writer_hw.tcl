# cam_frame_writer_hw.tcl
#
# Platform Designer component descriptor for the camera frame source and DDR
# frame writer (hdl/cam_frame_writer.vhd, with d5m_capture and async_fifo).
#
# Interfaces:
#   - avs: Avalon-MM slave (control/status registers, on the LW bridge)
#   - avm: 64-bit Avalon-MM burst master with write responses (frame writes
#          into HPS DDR through the FPGA-to-HPS AXI bridge)
#   - d5m: conduit to the TRDB-D5M pins, exported to the top level. PIXCLK is
#          a plain conduit signal: it clocks only logic inside this component,
#          and quartus/de10_nano.sdc constrains it.

package require -exact qsys 12.0

set_module_property NAME                 cam_frame_writer
set_module_property DISPLAY_NAME         "Camera Frame Writer"
set_module_property VERSION              2.0
set_module_property DESCRIPTION          "D5M capture and test-pattern generator writing frames into HPS DDR for camd"
set_module_property GROUP                "Custom"
set_module_property AUTHOR               "cvsoc"
set_module_property INSTANTIATE_IN_SYSTEM_MODULE true
set_module_property EDITABLE             false

# ── Source files ──────────────────────────────────────────────────────────────
add_fileset          QUARTUS_SYNTH QUARTUS_SYNTH "" ""
set_fileset_property QUARTUS_SYNTH TOP_LEVEL    cam_frame_writer
add_fileset_file     cam_pkg.vhd          VHDL PATH ../hdl/cam_pkg.vhd
add_fileset_file     async_fifo.vhd       VHDL PATH ../hdl/async_fifo.vhd
add_fileset_file     d5m_capture.vhd      VHDL PATH ../hdl/d5m_capture.vhd
add_fileset_file     cam_frame_writer.vhd VHDL PATH ../hdl/cam_frame_writer.vhd TOP_LEVEL_FILE

# ── Parameters ────────────────────────────────────────────────────────────────
add_parameter          SENSOR_WIDTH POSITIVE 2592
set_parameter_property SENSOR_WIDTH HDL_PARAMETER true
set_parameter_property SENSOR_WIDTH DESCRIPTION "Sensor width in pixels (color-bar geometry)"

# ── Clock interface ───────────────────────────────────────────────────────────
add_interface        clk clock end
set_interface_property clk ENABLED true
add_interface_port   clk clk clk Input 1

# ── Reset interface ───────────────────────────────────────────────────────────
add_interface        reset reset end
set_interface_property reset associatedClock clk
set_interface_property reset ENABLED true
add_interface_port   reset reset_n reset_n Input 1

# ── Avalon-MM Slave interface (control/status registers) ──────────────────────
add_interface        avs avalon end
set_interface_property avs associatedClock        clk
set_interface_property avs associatedReset        reset
set_interface_property avs addressUnits           WORDS
set_interface_property avs readWaitTime           0
set_interface_property avs writeWaitTime          0
set_interface_property avs ENABLED                true

add_interface_port   avs avs_address   address   Input  5
add_interface_port   avs avs_read      read      Input  1
add_interface_port   avs avs_readdata  readdata  Output 32
add_interface_port   avs avs_write     write     Input  1
add_interface_port   avs avs_writedata writedata Input  32

# ── Avalon-MM Master interface (frame writes) ─────────────────────────────────
add_interface        avm avalon start
set_interface_property avm associatedClock                  clk
set_interface_property avm associatedReset                  reset
set_interface_property avm addressUnits                     SYMBOLS
set_interface_property avm burstOnBurstBoundariesOnly       false
set_interface_property avm linewrapBursts                   false
set_interface_property avm doStreamWrites                   false
set_interface_property avm maximumPendingWriteTransactions  32
set_interface_property avm ENABLED                          true

add_interface_port   avm avm_address            address            Output 32
add_interface_port   avm avm_burstcount         burstcount         Output 5
add_interface_port   avm avm_write              write              Output 1
add_interface_port   avm avm_writedata          writedata          Output 64
add_interface_port   avm avm_byteenable         byteenable         Output 8
add_interface_port   avm avm_waitrequest        waitrequest        Input  1
add_interface_port   avm avm_writeresponsevalid writeresponsevalid Input  1
add_interface_port   avm avm_response           response           Input  2

# ── Conduit: TRDB-D5M on GPIO header JP1 ──────────────────────────────────────
add_interface        d5m conduit end
set_interface_property d5m ENABLED true

add_interface_port   d5m d5m_pixclk  pixclk  Input  1
add_interface_port   d5m d5m_d       d       Input  12
add_interface_port   d5m d5m_fval    fval    Input  1
add_interface_port   d5m d5m_lval    lval    Input  1
add_interface_port   d5m d5m_strobe  strobe  Input  1
add_interface_port   d5m d5m_xclkin  xclkin  Output 1
add_interface_port   d5m d5m_reset_n reset_n Output 1
add_interface_port   d5m d5m_trigger trigger Output 1
add_interface_port   d5m d5m_scl_oe  scl_oe  Output 1
add_interface_port   d5m d5m_sda_oe  sda_oe  Output 1
add_interface_port   d5m d5m_scl_in  scl_in  Input  1
add_interface_port   d5m d5m_sda_in  sda_in  Input  1
