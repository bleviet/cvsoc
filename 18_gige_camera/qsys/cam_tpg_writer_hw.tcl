# cam_tpg_writer_hw.tcl
#
# Platform Designer component descriptor for the camd test-pattern generator
# and DDR frame writer (hdl/cam_tpg_writer.vhd).
#
# The component has two Avalon-MM interfaces:
#   - avs: slave (control/status registers, on the LW bridge)
#   - avm: 64-bit burst master with write responses (frame writes into HPS DDR
#          through the FPGA-to-HPS AXI bridge)

package require -exact qsys 12.0

set_module_property NAME                 cam_tpg_writer
set_module_property DISPLAY_NAME         "Camera Test Pattern Writer"
set_module_property VERSION              1.0
set_module_property DESCRIPTION          "Test-pattern generator writing frames into HPS DDR for camd"
set_module_property GROUP                "Custom"
set_module_property AUTHOR               "cvsoc"
set_module_property INSTANTIATE_IN_SYSTEM_MODULE true
set_module_property EDITABLE             false

# ── Source files ──────────────────────────────────────────────────────────────
add_fileset          QUARTUS_SYNTH QUARTUS_SYNTH "" ""
set_fileset_property QUARTUS_SYNTH TOP_LEVEL    cam_tpg_writer
add_fileset_file     cam_tpg_writer.vhd VHDL PATH ../hdl/cam_tpg_writer.vhd TOP_LEVEL_FILE

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

add_interface_port   avs avs_address   address   Input  4
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
