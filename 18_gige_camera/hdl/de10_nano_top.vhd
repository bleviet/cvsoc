library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

--------------------------------------------------------------------------------
-- Top-level wrapper for 18_gige_camera on the DE10-Nano board.
--
-- The Platform Designer system (hps_system) includes:
--   - The ARM Cortex-A9 HPS with its DDR3 SDRAM controller
--   - A Lightweight HPS-to-FPGA AXI bridge (camera CSRs at 0xFF200000)
--   - An FPGA-to-HPS AXI bridge carrying the frame writes into DDR
--   - cam_frame_writer: D5M capture + test-pattern generator + frame writer
--
-- The TRDB-D5M sits on GPIO header JP1 (GPIO_0). Its I2C lines are open
-- drain: the FPGA only ever pulls them low, and pull-ups (on the D5M, plus
-- the FPGA's weak pull-ups) take them high.
--
-- The FPGA_CLK1_50 oscillator clocks all FPGA fabric logic.
-- KEY[0] (active-low) resets the fabric.
-- LED[0] blinks at ~1.5 Hz to show the bitstream is loaded and clocked.
--------------------------------------------------------------------------------
entity de10_nano_top is
  port (
    -- 50 MHz FPGA oscillator
    fpga_clk1_50 : in    std_logic;

    -- Push-button KEY[0] (active-low) — FPGA fabric reset
    key          : in    std_logic_vector(0 downto 0);

    -- Board LEDs
    led          : out   std_logic_vector(7 downto 0);

    -- ── TRDB-D5M camera on GPIO header JP1 ────────────────────────────────
    d5m_pixclk   : in    std_logic;
    d5m_d        : in    std_logic_vector(11 downto 0);
    d5m_fval     : in    std_logic;
    d5m_lval     : in    std_logic;
    d5m_strobe   : in    std_logic;
    d5m_xclkin   : out   std_logic;
    d5m_reset_n  : out   std_logic;
    d5m_trigger  : out   std_logic;
    d5m_sclk     : inout std_logic;
    d5m_sdata    : inout std_logic;

    -- ── HPS DDR3 SDRAM interface ──────────────────────────────────────────
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
      clk_clk            : in    std_logic;
      reset_reset_n      : in    std_logic;
      memory_mem_a       : out   std_logic_vector(14 downto 0);
      memory_mem_ba      : out   std_logic_vector(2 downto 0);
      memory_mem_ck      : out   std_logic;
      memory_mem_ck_n    : out   std_logic;
      memory_mem_cke     : out   std_logic;
      memory_mem_cs_n    : out   std_logic;
      memory_mem_ras_n   : out   std_logic;
      memory_mem_cas_n   : out   std_logic;
      memory_mem_we_n    : out   std_logic;
      memory_mem_reset_n : out   std_logic;
      memory_mem_dq      : inout std_logic_vector(31 downto 0);
      memory_mem_dqs     : inout std_logic_vector(3 downto 0);
      memory_mem_dqs_n   : inout std_logic_vector(3 downto 0);
      memory_mem_odt     : out   std_logic;
      memory_mem_dm      : out   std_logic_vector(3 downto 0);
      memory_oct_rzqin   : in    std_logic;
      d5m_pixclk         : in    std_logic;
      d5m_d              : in    std_logic_vector(11 downto 0);
      d5m_fval           : in    std_logic;
      d5m_lval           : in    std_logic;
      d5m_strobe         : in    std_logic;
      d5m_xclkin         : out   std_logic;
      d5m_reset_n        : out   std_logic;
      d5m_trigger        : out   std_logic;
      d5m_scl_oe         : out   std_logic;
      d5m_sda_oe         : out   std_logic;
      d5m_scl_in         : in    std_logic;
      d5m_sda_in         : in    std_logic
    );
  end component hps_system;

  signal heartbeat : unsigned(24 downto 0) := (others => '0');
  signal scl_oe    : std_logic;
  signal sda_oe    : std_logic;

begin

  process (fpga_clk1_50)
  begin
    if rising_edge(fpga_clk1_50) then
      heartbeat <= heartbeat + 1;
    end if;
  end process;

  led <= "0000000" & heartbeat(heartbeat'high);

  -- Open-drain I2C to the sensor
  d5m_sclk  <= '0' when scl_oe = '1' else 'Z';
  d5m_sdata <= '0' when sda_oe = '1' else 'Z';

  hps_system_inst : hps_system
    port map (
      clk_clk            => fpga_clk1_50,
      reset_reset_n      => key(0),
      -- DDR3 SDRAM
      memory_mem_a       => hps_ddr3_addr,
      memory_mem_ba      => hps_ddr3_ba,
      memory_mem_ck      => hps_ddr3_ck_p,
      memory_mem_ck_n    => hps_ddr3_ck_n,
      memory_mem_cke     => hps_ddr3_cke,
      memory_mem_cs_n    => hps_ddr3_cs_n,
      memory_mem_ras_n   => hps_ddr3_ras_n,
      memory_mem_cas_n   => hps_ddr3_cas_n,
      memory_mem_we_n    => hps_ddr3_we_n,
      memory_mem_reset_n => hps_ddr3_reset_n,
      memory_mem_dq      => hps_ddr3_dq,
      memory_mem_dqs     => hps_ddr3_dqs_p,
      memory_mem_dqs_n   => hps_ddr3_dqs_n,
      memory_mem_odt     => hps_ddr3_odt,
      memory_mem_dm      => hps_ddr3_dm,
      memory_oct_rzqin   => hps_ddr3_rzq,
      -- TRDB-D5M
      d5m_pixclk         => d5m_pixclk,
      d5m_d              => d5m_d,
      d5m_fval           => d5m_fval,
      d5m_lval           => d5m_lval,
      d5m_strobe         => d5m_strobe,
      d5m_xclkin         => d5m_xclkin,
      d5m_reset_n        => d5m_reset_n,
      d5m_trigger        => d5m_trigger,
      d5m_scl_oe         => scl_oe,
      d5m_sda_oe         => sda_oe,
      d5m_scl_in         => d5m_sclk,
      d5m_sda_in         => d5m_sdata
    );

end architecture rtl;
