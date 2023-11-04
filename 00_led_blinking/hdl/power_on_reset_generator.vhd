library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity power_on_reset_generator is
  port (
    clk_i : in std_logic;
    por_o : out std_logic
  );
end entity power_on_reset_generator;

architecture rtl of power_on_reset_generator is
  signal power_on_reset : std_logic                     := '1';
  signal counter        : natural range 0 to 1000000000 := 0;
begin

  por_o <= power_on_reset;

  --------------------------------------------------------------------------------
  -- POWER ON RESET GENERATION
  --------------------------------------------------------------------------------
  por_gen_proc : process (clk_i)
  begin
    if rising_edge(clk_i) then
      if counter < 1000000000 then
        counter        <= counter + 1;
        power_on_reset <= '1';
      else
        power_on_reset <= '0';
      end if;
    end if;
  end process por_gen_proc;

end architecture rtl;
