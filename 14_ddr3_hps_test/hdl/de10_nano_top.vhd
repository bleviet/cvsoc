library ieee;
use ieee.std_logic_1164.all;

--------------------------------------------------------------------------------
-- Top-level wrapper for the DDR3 HPS memory test on the DE10-Nano board.
--
-- The design is identical to 05_hps_led — the DDR3 controller is part of
-- the HPS hard block and requires no additional FPGA logic.  The LED PIO
-- is used by the bare-metal ARM application to indicate test progress and
-- pass/fail status.
--------------------------------------------------------------------------------
entity de10_nano_top is
  port (
    -- 50 MHz FPGA oscillator — clocks the FPGA fabric and the LW H2F bridge
    fpga_clk1_50 : in    std_logic;

    -- Push-button KEY[0] (active-low) — FPGA fabric reset
    key          : in    std_logic_vector(0 downto 0);

    -- Board LEDs driven by the HPS to indicate test status
    led          : out   std_logic_vector(7 downto 0);

    -- ── HPS DDR3 SDRAM interface ──────────────────────────────────────────
    -- These signals go directly to the on-board DDR3L chips.
    -- Pin locations are fixed by the Cyclone V SoC package; Quartus assigns
    -- them automatically when the HPS component is instantiated.
    hps_ddr3_addr    : out   std_logic_vector(14 downto 0);
    hps_ddr3_ba      : out   std_logic_vector(2 downto 0);
    hps_ddr3_cas_n   : out   std_logic;
    hps_ddr3_ck_p    : out   std_logic;
    hps_ddr3_ck_n    : out   std_logic;
    hps_ddr3_cke     : out   std_logic;
    hps_ddr3_cs_n    : out   std_logic;
    hps_ddr3_dm      : out   std_logic_vector(3 downto 0);
    hps_ddr3_dq      : inout std_logic_vector(31 downto 0);
    hps_ddr3_dqs_p   : inout std_logic_vector(3 downto 0);
    hps_ddr3_dqs_n   : inout std_logic_vector(3 downto 0);
    hps_ddr3_odt     : out   std_logic;
    hps_ddr3_ras_n   : out   std_logic;
    hps_ddr3_reset_n : out   std_logic;
    hps_ddr3_we_n    : out   std_logic;
    hps_ddr3_rzq     : in    std_logic
  );
end entity de10_nano_top;

architecture rtl of de10_nano_top is

  component hps_system is
    port (
      clk_clk                        : in    std_logic;
      reset_reset_n                  : in    std_logic;
      led_external_connection_export : out   std_logic_vector(7 downto 0);
      memory_mem_a                   : out   std_logic_vector(14 downto 0);
      memory_mem_ba                  : out   std_logic_vector(2 downto 0);
      memory_mem_ck                  : out   std_logic;
      memory_mem_ck_n                : out   std_logic;
      memory_mem_cke                 : out   std_logic;
      memory_mem_cs_n                : out   std_logic;
      memory_mem_ras_n               : out   std_logic;
      memory_mem_cas_n               : out   std_logic;
      memory_mem_we_n                : out   std_logic;
      memory_mem_reset_n             : out   std_logic;
      memory_mem_dq                  : inout std_logic_vector(31 downto 0);
      memory_mem_dqs                 : inout std_logic_vector(3 downto 0);
      memory_mem_dqs_n               : inout std_logic_vector(3 downto 0);
      memory_mem_odt                 : out   std_logic;
      memory_mem_dm                  : out   std_logic_vector(3 downto 0);
      memory_oct_rzqin               : in    std_logic
    );
  end component hps_system;

begin

  hps_system_inst : hps_system
    port map (
      clk_clk                        => fpga_clk1_50,
      reset_reset_n                  => key(0),
      led_external_connection_export => led,
      -- DDR3 SDRAM
      memory_mem_a                   => hps_ddr3_addr,
      memory_mem_ba                  => hps_ddr3_ba,
      memory_mem_ck                  => hps_ddr3_ck_p,
      memory_mem_ck_n                => hps_ddr3_ck_n,
      memory_mem_cke                 => hps_ddr3_cke,
      memory_mem_cs_n                => hps_ddr3_cs_n,
      memory_mem_ras_n               => hps_ddr3_ras_n,
      memory_mem_cas_n               => hps_ddr3_cas_n,
      memory_mem_we_n                => hps_ddr3_we_n,
      memory_mem_reset_n             => hps_ddr3_reset_n,
      memory_mem_dq                  => hps_ddr3_dq,
      memory_mem_dqs                 => hps_ddr3_dqs_p,
      memory_mem_dqs_n               => hps_ddr3_dqs_n,
      memory_mem_odt                 => hps_ddr3_odt,
      memory_mem_dm                  => hps_ddr3_dm,
      memory_oct_rzqin               => hps_ddr3_rzq
    );

end architecture rtl;
