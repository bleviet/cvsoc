library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

--------------------------------------------------------------------------------
-- async_fifo — dual-clock FIFO with a first-word-fall-through read port.
--
-- Carries d5m_capture's words from the sensor's PIXCLK domain into the fabric
-- clock domain. The pointers cross clock domains as Gray codes through
-- two-flop synchronizers (constrained in quartus/de10_nano.sdc), so `full`
-- and `rd_count` are conservative: they lag the other side by a few cycles.
--
-- There is no reset. The pointers start at zero when the FPGA is configured,
-- and the write clock (PIXCLK) only runs once camd has started the sensor.
--
-- Read port: `rd_data` is valid while `rd_empty` = '0'. Pulse `rd_en` to drop
-- the current word; the next one appears on the following cycle.
--------------------------------------------------------------------------------
entity async_fifo is
  generic (
    WIDTH      : positive := 68;
    ADDR_WIDTH : positive := 9      -- depth = 2**ADDR_WIDTH words
  );
  port (
    -- ── Write side ─────────────────────────────────────────────────────
    wr_clk   : in  std_logic;
    wr_en    : in  std_logic;
    wr_data  : in  std_logic_vector(WIDTH - 1 downto 0);
    wr_full  : out std_logic;

    -- ── Read side ──────────────────────────────────────────────────────
    rd_clk   : in  std_logic;
    rd_en    : in  std_logic;
    rd_data  : out std_logic_vector(WIDTH - 1 downto 0);
    rd_empty : out std_logic;
    rd_count : out unsigned(ADDR_WIDTH downto 0)   -- words available to read
  );
end entity async_fifo;

architecture rtl of async_fifo is

  subtype ptr_t is unsigned(ADDR_WIDTH downto 0);   -- one extra bit: full vs empty
  type ram_t is array (0 to 2**ADDR_WIDTH - 1) of std_logic_vector(WIDTH - 1 downto 0);
  signal ram : ram_t;

  function to_gray(b : ptr_t) return ptr_t is
  begin
    return b xor shift_right(b, 1);
  end function;

  function from_gray(g : ptr_t) return ptr_t is
    variable b : ptr_t;
  begin
    b(ADDR_WIDTH) := g(ADDR_WIDTH);
    for i in ADDR_WIDTH - 1 downto 0 loop
      b(i) := b(i + 1) xor g(i);
    end loop;
    return b;
  end function;

  -- Write domain
  signal wbin, wgray     : ptr_t := (others => '0');
  signal rgray_s1        : ptr_t := (others => '0');
  signal rgray_s2        : ptr_t := (others => '0');
  signal full_i          : std_logic := '0';

  -- Read domain
  signal rbin, rgray     : ptr_t := (others => '0');
  signal wgray_s1        : ptr_t := (others => '0');
  signal wgray_s2        : ptr_t := (others => '0');
  signal dout            : std_logic_vector(WIDTH - 1 downto 0) := (others => '0');
  signal dout_valid      : std_logic := '0';

  -- Mark the synchronizers for Quartus' metastability analysis.
  attribute altera_attribute : string;
  attribute altera_attribute of rgray_s1 : signal is
    "-name SYNCHRONIZER_IDENTIFICATION FORCED_IF_ASYNCHRONOUS";
  attribute altera_attribute of wgray_s1 : signal is
    "-name SYNCHRONIZER_IDENTIFICATION FORCED_IF_ASYNCHRONOUS";

begin

  -- ── Write side ──────────────────────────────────────────────────────────
  process (wr_clk)
    variable wbin_next : ptr_t;
  begin
    if rising_edge(wr_clk) then
      rgray_s1 <= rgray;
      rgray_s2 <= rgray_s1;

      wbin_next := wbin;
      if wr_en = '1' and full_i = '0' then
        ram(to_integer(wbin(ADDR_WIDTH - 1 downto 0))) <= wr_data;
        wbin_next := wbin + 1;
      end if;
      wbin  <= wbin_next;
      wgray <= to_gray(wbin_next);
      -- Full: write pointer one lap ahead of the (synchronized) read pointer,
      -- i.e. the Gray codes differ only in their two top bits.
      if to_gray(wbin_next) = (not rgray_s2(ADDR_WIDTH downto ADDR_WIDTH - 1)) &
                              rgray_s2(ADDR_WIDTH - 2 downto 0) then
        full_i <= '1';
      else
        full_i <= '0';
      end if;
    end if;
  end process;

  wr_full <= full_i;

  -- ── Read side ───────────────────────────────────────────────────────────
  -- The output register holds the head word. It is refilled from RAM when it
  -- is empty or being consumed, so a word falls through in two cycles.
  process (rd_clk)
    variable rbin_next : ptr_t;
  begin
    if rising_edge(rd_clk) then
      wgray_s1 <= wgray;
      wgray_s2 <= wgray_s1;

      rbin_next := rbin;
      if dout_valid = '0' or rd_en = '1' then
        if rgray /= wgray_s2 then
          dout       <= ram(to_integer(rbin(ADDR_WIDTH - 1 downto 0)));
          dout_valid <= '1';
          rbin_next  := rbin + 1;
        else
          dout_valid <= '0';
        end if;
      end if;
      rbin  <= rbin_next;
      rgray <= to_gray(rbin_next);
    end if;
  end process;

  rd_data  <= dout;
  rd_empty <= not dout_valid;
  rd_count <= from_gray(wgray_s2) - rbin + 1 when dout_valid = '1' else
              from_gray(wgray_s2) - rbin;

end architecture rtl;
