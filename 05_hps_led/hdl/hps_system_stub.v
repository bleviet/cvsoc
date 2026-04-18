// Black-box stub for hps_system.
//
// Provides a module declaration so that de10_nano_top.v can be elaborated
// without error.  The actual implementation is supplied by Quartus when it
// processes hps_system.qsys (QSYS_FILE) using its built-in IP integration.
//
// The body is intentionally empty; Quartus treats this as a black box and
// substitutes the Platform Designer netlist at the Fitter stage.
//
// AUTO-GENERATED – do not modify by hand.  Regenerate with:
//   qsys-generate hps_system.qsys --synthesis=VERILOG
// and copy only the port list from hps_system/synthesis/hps_system.v.

/* synthesis synthesis_off */
module hps_system (
    input  wire        clk_clk,
    output wire [7:0]  led_external_connection_export,
    output wire [14:0] memory_mem_a,
    output wire [2:0]  memory_mem_ba,
    output wire        memory_mem_ck,
    output wire        memory_mem_ck_n,
    output wire        memory_mem_cke,
    output wire        memory_mem_cs_n,
    output wire        memory_mem_ras_n,
    output wire        memory_mem_cas_n,
    output wire        memory_mem_we_n,
    output wire        memory_mem_reset_n,
    inout  wire [31:0] memory_mem_dq,
    inout  wire [3:0]  memory_mem_dqs,
    inout  wire [3:0]  memory_mem_dqs_n,
    output wire        memory_mem_odt,
    output wire [3:0]  memory_mem_dm,
    input  wire        memory_oct_rzqin,
    input  wire        reset_reset_n
);
endmodule
/* synthesis synthesis_on */
