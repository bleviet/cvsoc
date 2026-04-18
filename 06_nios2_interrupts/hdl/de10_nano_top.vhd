library ieee;
use ieee.std_logic_1164.all;

--------------------------------------------------------------------------------
-- Top-level wrapper for the Nios II interrupt demo on the DE10-Nano board.
--
-- Extends 04_nios2_led: adds KEY[0] and KEY[1] push-button inputs that are
-- wired to the button_pio peripheral.  The Nios II firmware registers an ISR
-- (via the HAL alt_ic_isr_register API) that fires on each button press.
--------------------------------------------------------------------------------
entity de10_nano_top is
  port (
    fpga_clk1_50 : in  std_logic;
    -- Push buttons (active-LOW).  KEY[0] = bit 0, KEY[1] = bit 1.
    key          : in  std_logic_vector(1 downto 0);
    led          : out std_logic_vector(7 downto 0)
  );
end entity de10_nano_top;

architecture rtl of de10_nano_top is

  component nios2_system is
    port (
      clk_clk                           : in  std_logic;
      reset_reset                       : in  std_logic;
      led_external_connection_export    : out std_logic_vector(7 downto 0);
      button_external_connection_export : in  std_logic_vector(1 downto 0)
    );
  end component nios2_system;

  signal power_on_reset : std_logic;

begin

  power_on_reset_generator_inst : entity work.power_on_reset_generator
    generic map (
      G_CLK_FREQ_HZ       => 50_000_000,
      G_RESET_DURATION_NS => 1_000_000
    )
    port map (
      clk_i => fpga_clk1_50,
      por_o => power_on_reset
    );

  nios2_system_inst : nios2_system
    port map (
      clk_clk                           => fpga_clk1_50,
      reset_reset                       => power_on_reset,
      led_external_connection_export    => led,
      button_external_connection_export => key
    );

end architecture rtl;
