library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity led_running is
  generic (
    G_CLK_FREQ_HZ : integer := 50000000;
    G_RUNNING_FREQ_HZ : integer := 1
  );
  port (
    clk_i : in std_logic;
    rst_i : in std_logic;
    led_o : out std_logic_vector(7 downto 0)
  );
end entity led_running;

architecture rtl of led_running is
  constant C_COUNTER_MAX : integer := G_CLK_FREQ_HZ / (2 * G_RUNNING_FREQ_HZ);

  signal counter : natural range 0 to C_COUNTER_MAX - 1 := 0;
  signal led_running : std_logic_vector(7 downto 0) := x"01";
begin

  led_o <= led_running;

  --------------------------------------------------------------------------------
  -- LED TOGGLING PROCESS
  --------------------------------------------------------------------------------
  led_running_proc : process(clk_i)
  begin
    if rising_edge(clk_i) then
      if rst_i then
        counter <= 0;
      else
        if counter = C_COUNTER_MAX - 1 then
          counter <= 0;

          led_running <= std_logic_vector(unsigned(led_running) sll 1);
          if led_running = x"00" then
            led_running <= x"01";
          end if;
        else
          counter <= counter + 1;
        end if;

      end if;
    end if;
  end process led_running_proc;

end architecture rtl;
