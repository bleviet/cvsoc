# ddr3_test_master_hw.tcl
#
# Platform Designer component descriptor for the DDR3 test master.
# This file tells qsys-script how to instantiate the custom VHDL component.
#
# The component has two Avalon-MM interfaces:
#   - avs: slave (control/status registers, on the LW bridge)
#   - avm: master (DDR3 access, on the F2H SDRAM bridge)

package require -exact qsys 12.0

set_module_property NAME                 ddr3_test_master
set_module_property DISPLAY_NAME         "DDR3 Test Master"
set_module_property VERSION              1.0
set_module_property DESCRIPTION          "Avalon-MM master for FPGA-side DDR3 memory test"
set_module_property GROUP                "Custom"
set_module_property AUTHOR               "cvsoc"
set_module_property INSTANTIATE_IN_SYSTEM_MODULE true
set_module_property EDITABLE             false

# ── Source files ──────────────────────────────────────────────────────────────
add_fileset          QUARTUS_SYNTH QUARTUS_SYNTH "" ""
set_fileset_property QUARTUS_SYNTH TOP_LEVEL    ddr3_test_master
add_fileset_file     ddr3_test_master.vhd VHDL PATH ../hdl/ddr3_test_master.vhd TOP_LEVEL_FILE

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

add_interface_port   avs avs_address   address   Input  3
add_interface_port   avs avs_read      read      Input  1
add_interface_port   avs avs_readdata  readdata  Output 32
add_interface_port   avs avs_write     write     Input  1
add_interface_port   avs avs_writedata writedata Input  32

# ── Avalon-MM Master interface (DDR3 access) ──────────────────────────────────
add_interface        avm avalon start
set_interface_property avm associatedClock        clk
set_interface_property avm associatedReset        reset
set_interface_property avm addressUnits           SYMBOLS
set_interface_property avm ENABLED                true

add_interface_port   avm avm_address       address       Output 32
add_interface_port   avm avm_read          read          Output 1
add_interface_port   avm avm_readdata      readdata      Input  32
add_interface_port   avm avm_readdatavalid readdatavalid Input  1
add_interface_port   avm avm_write         write         Output 1
add_interface_port   avm avm_writedata     writedata     Output 32
add_interface_port   avm avm_waitrequest   waitrequest   Input  1
add_interface_port   avm avm_byteenable    byteenable    Output 4
