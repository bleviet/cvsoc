# hdmi_preview_hw.tcl
#
# Platform Designer component descriptor for the HDMI preview
# (hdl/hdmi_preview.vhd): shows the camera's latest frame on the ADV7513.
#
# Interfaces:
#   - clk/reset: fabric clock (CSRs, line fetcher)
#   - pclk:      pixel clock, 74.25 MHz for 720p60
#   - avs:       Avalon-MM slave (control/status registers, on the LW bridge)
#   - avm:       64-bit Avalon-MM burst read master (frame reads from HPS DDR
#                through the FPGA-to-HPS AXI bridge)
#   - frame:     conduit from cam_frame_writer (last completed frame)
#   - hdmi:      conduit to the ADV7513 pins, exported to the top level

package require -exact qsys 12.0

set_module_property NAME                 hdmi_preview
set_module_property DISPLAY_NAME         "HDMI Preview"
set_module_property VERSION              1.0
set_module_property DESCRIPTION          "Shows the camera's latest frame from DDR on the ADV7513 (720p60)"
set_module_property GROUP                "Custom"
set_module_property AUTHOR               "cvsoc"
set_module_property INSTANTIATE_IN_SYSTEM_MODULE true
set_module_property EDITABLE             false

# ── Source files ──────────────────────────────────────────────────────────────
add_fileset          QUARTUS_SYNTH QUARTUS_SYNTH "" ""
set_fileset_property QUARTUS_SYNTH TOP_LEVEL    hdmi_preview
add_fileset_file     hdmi_preview.vhd VHDL PATH ../hdl/hdmi_preview.vhd TOP_LEVEL_FILE

# ── Clock and reset interfaces ────────────────────────────────────────────────
add_interface        clk clock end
set_interface_property clk ENABLED true
add_interface_port   clk clk clk Input 1

add_interface        reset reset end
set_interface_property reset associatedClock clk
set_interface_property reset ENABLED true
add_interface_port   reset reset_n reset_n Input 1

add_interface        pclk clock end
set_interface_property pclk ENABLED true
add_interface_port   pclk pclk clk Input 1

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

# ── Avalon-MM Master interface (frame reads) ──────────────────────────────────
add_interface        avm avalon start
set_interface_property avm associatedClock                 clk
set_interface_property avm associatedReset                 reset
set_interface_property avm addressUnits                    SYMBOLS
set_interface_property avm burstOnBurstBoundariesOnly      false
set_interface_property avm linewrapBursts                  false
set_interface_property avm doStreamReads                   false
set_interface_property avm maximumPendingReadTransactions  32
set_interface_property avm ENABLED                         true

add_interface_port   avm avm_address       address       Output 32
add_interface_port   avm avm_burstcount    burstcount    Output 5
add_interface_port   avm avm_read          read          Output 1
add_interface_port   avm avm_readdata      readdata      Input  64
add_interface_port   avm avm_readdatavalid readdatavalid Input  1
add_interface_port   avm avm_waitrequest   waitrequest   Input  1

# ── Conduit: last completed frame, from cam_frame_writer ──────────────────────
add_interface        frame conduit end
set_interface_property frame ENABLED true
add_interface_port   frame frame_addr   addr   Input 32
add_interface_port   frame frame_width  width  Input 16
add_interface_port   frame frame_height height Input 16
add_interface_port   frame frame_valid  valid  Input 1

# ── Conduit: ADV7513 ──────────────────────────────────────────────────────────
add_interface        hdmi conduit end
set_interface_property hdmi ENABLED true
add_interface_port   hdmi hdmi_clk    tx_clk Output 1
add_interface_port   hdmi hdmi_d      tx_d   Output 24
add_interface_port   hdmi hdmi_de     tx_de  Output 1
add_interface_port   hdmi hdmi_hs     tx_hs  Output 1
add_interface_port   hdmi hdmi_vs     tx_vs  Output 1
add_interface_port   hdmi hdmi_int    tx_int Input  1
add_interface_port   hdmi hdmi_scl_oe scl_oe Output 1
add_interface_port   hdmi hdmi_sda_oe sda_oe Output 1
add_interface_port   hdmi hdmi_scl_in scl_in Input  1
add_interface_port   hdmi hdmi_sda_in sda_in Input  1
