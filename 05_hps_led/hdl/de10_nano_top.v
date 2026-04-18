// Top-level Verilog wrapper for the HPS LED demo on the DE10-Nano board.
//
// The design contains a single Platform Designer system (hps_system) that
// includes:
//   - The ARM Cortex-A9 HPS with its DDR3 SDRAM controller
//   - A Lightweight HPS-to-FPGA AXI bridge (driven at 50 MHz)
//   - An 8-bit LED PIO slave hanging off that bridge
//
// FPGA_CLK1_50 clocks both the FPGA fabric PIO logic and the HPS lightweight
// bridge interface.  KEY[0] (active-low) resets the fabric.

module de10_nano_top (
    // 50 MHz FPGA oscillator
    input              fpga_clk1_50,

    // Push-button KEY[0] (active-low) — FPGA fabric reset
    input              key0,

    // Board LEDs driven by HPS via LED PIO
    output      [7:0]  led,

    // ── HPS DDR3 SDRAM interface ─────────────────────────────────────────
    output      [14:0] hps_ddr3_addr,
    output      [2:0]  hps_ddr3_ba,
    output             hps_ddr3_cas_n,
    output             hps_ddr3_ck_p,
    output             hps_ddr3_ck_n,
    output             hps_ddr3_cke,
    output             hps_ddr3_cs_n,
    output      [3:0]  hps_ddr3_dm,
    inout       [31:0] hps_ddr3_dq,
    inout       [3:0]  hps_ddr3_dqs_p,
    inout       [3:0]  hps_ddr3_dqs_n,
    output             hps_ddr3_odt,
    output             hps_ddr3_ras_n,
    output             hps_ddr3_reset_n,
    output             hps_ddr3_we_n,
    input              hps_ddr3_rzq
);

hps_system hps_system_inst (
    // FPGA fabric clock (50 MHz) → LED PIO and LW bridge clock domain
    .clk_clk                        (fpga_clk1_50),
    // Active-low reset from KEY[0]
    .reset_reset_n                  (key0),
    // LED PIO output → board LEDs
    .led_external_connection_export (led),
    // DDR3 SDRAM
    .memory_mem_a                   (hps_ddr3_addr),
    .memory_mem_ba                  (hps_ddr3_ba),
    .memory_mem_ck                  (hps_ddr3_ck_p),
    .memory_mem_ck_n                (hps_ddr3_ck_n),
    .memory_mem_cke                 (hps_ddr3_cke),
    .memory_mem_cs_n                (hps_ddr3_cs_n),
    .memory_mem_ras_n               (hps_ddr3_ras_n),
    .memory_mem_cas_n               (hps_ddr3_cas_n),
    .memory_mem_we_n                (hps_ddr3_we_n),
    .memory_mem_reset_n             (hps_ddr3_reset_n),
    .memory_mem_dq                  (hps_ddr3_dq),
    .memory_mem_dqs                 (hps_ddr3_dqs_p),
    .memory_mem_dqs_n               (hps_ddr3_dqs_n),
    .memory_mem_odt                 (hps_ddr3_odt),
    .memory_mem_dm                  (hps_ddr3_dm),
    .memory_oct_rzqin               (hps_ddr3_rzq)
);

endmodule
