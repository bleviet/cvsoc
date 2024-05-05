library ieee;
use ieee.std_logic_1164.all;

--------------------------------------------------------------------------------
-- Entity declaration for the top-level module
--------------------------------------------------------------------------------
entity de10_nano_top is
  port (
    fpga_clk1_50 : in std_logic;
    led          : out std_logic_vector(7 downto 0)
  );
end entity de10_nano_top;

--------------------------------------------------------------------------------
-- Architecture declaration for the top-level module
--------------------------------------------------------------------------------
architecture rtl of de10_nano_top is
  signal power_on_reset : std_logic;
begin

  --------------------------------------------------------------------------------
  -- POWER ON RESET GENERATOR INSTANTIATION
  --------------------------------------------------------------------------------
  power_on_reset_generator_inst : entity work.power_on_reset_generator
    generic map (
      G_CLK_FREQ_HZ       => 50000000,
      G_RESET_DURATION_NS => 1000000
    )
    port map (
      clk_i => fpga_clk1_50,
      por_o => power_on_reset
    );

  --------------------------------------------------------------------------------
  -- LET THE LEDS BLINKING
  --------------------------------------------------------------------------------
  led_blinking_gen : for i in 0 to led'length-1 generate
    led_blinking_inst : entity work.led_blinking
      port map (
        clk_i => fpga_clk1_50,
        rst_i => power_on_reset,
        led_o => led(i)
      );
  end generate led_blinking_gen;
end architecture rtl;
