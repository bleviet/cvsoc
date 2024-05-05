library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity led_blinking is
  generic (
    G_CLK_FREQ_HZ : integer := 50000000;
    G_BLINK_FREQ_HZ : integer := 1
  );
  port (
    clk_i : in std_logic;
    rst_i : in std_logic;
    led_o : out std_logic
  );
end entity led_blinking;

architecture rtl of led_blinking is
  constant C_COUNTER_MAX : integer := G_CLK_FREQ_HZ / (2 * G_BLINK_FREQ_HZ);

  signal counter : natural range 0 to C_COUNTER_MAX - 1 := 0;
  signal led_toggle : std_logic := '0';
begin

  led_o <= led_toggle;

  --------------------------------------------------------------------------------
  -- LED TOGGLING PROCESS
  --------------------------------------------------------------------------------
  led_toggle_proc : process(clk_i)
  begin
    if rising_edge(clk_i) then
      if rst_i then
        counter <= 0;
      else
        if counter = C_COUNTER_MAX - 1 then
          counter <= 0;
          led_toggle <= not led_toggle;
        else
          counter <= counter + 1;
        end if;
      end if;
    end if;
  end process led_toggle_proc;

end architecture rtl;
