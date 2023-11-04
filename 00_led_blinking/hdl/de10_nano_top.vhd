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
begin
  
  
end architecture rtl;
