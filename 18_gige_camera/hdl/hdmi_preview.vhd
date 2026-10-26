library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

--------------------------------------------------------------------------------
-- hdmi_preview — shows the camera's latest frame on HDMI (ADV7513), for
-- aiming and focusing at the camera.
--
-- The display is a 1:1 window of H_ACTIVE x V_ACTIVE pixels (1280 x 720 at
-- 60 Hz by default) onto the last frame cam_frame_writer completed in DDR.
-- SRC_X/SRC_Y place the window in the frame; parts outside the frame are
-- black. BayerGR8 data is shown in color, one RGB value per 2x2 cell
-- (G R / B G), or as the raw values in grey.
--
-- Two clock domains:
--   clk   (50 MHz)  CSRs and the line fetcher, an Avalon-MM read master on
--                   the FPGA-to-HPS bridge
--   pclk  (74.25 MHz) video timing and the pixel pipeline
--
-- The display shows rows in pairs (a GR row and a BG row). While pair k is on
-- screen, the fetcher reads pair k + 1 into the other half of two dual-clock
-- line buffers (one for the GR row, one for the BG row). Requests and
-- completions cross the domains as toggles; the request's pair number is
-- stable for two lines when the toggle arrives.
--
-- Register map (slave interface, 32-bit words):
--   Offset  Name        Access  Description
--   0x00    ID          R/O     0x48444D31 ("HDM1")
--   0x04    CTRL        R/W     bit 0: show the frame (0: black screen)
--                               bit 1: grey (raw sensor values, no color)
--                               bit 2: color bars instead of the frame
--   0x08    STATUS      R/O     bit 0: a frame is available from the writer
--   0x0C    SRC_X       R/W     signed window left edge in the frame, multiple of 8
--   0x10    SRC_Y       R/W     signed window top edge in the frame, even
--   0x14    FRAMES      R/O     display frames shown
--   0x18    UNDERFLOWS  R/O     row pairs not fetched in time (shown stale)
--   0x1C    I2C         R/W     bit 0: pull SCL low, bit 1: pull SDA low (ADV7513)
--                               R/O bit 8: SCL pin, bit 9: SDA pin, bit 10: INT pin
--------------------------------------------------------------------------------
entity hdmi_preview is
  generic (
    H_ACTIVE : positive := 1280;    -- multiple of 8
    H_FP     : positive := 110;
    H_SYNC   : positive := 40;
    H_BP     : positive := 220;
    V_ACTIVE : positive := 720;     -- even
    V_FP     : positive := 5;
    V_SYNC   : positive := 5;
    V_BP     : positive := 20
  );
  port (
    clk           : in    std_logic;
    reset_n       : in    std_logic;
    pclk          : in    std_logic;

    -- ── Avalon-MM Slave interface (control/status registers) ──────────
    avs_address   : in    std_logic_vector(2 downto 0);
    avs_read      : in    std_logic;
    avs_readdata  : out   std_logic_vector(31 downto 0);
    avs_write     : in    std_logic;
    avs_writedata : in    std_logic_vector(31 downto 0);

    -- ── Avalon-MM Master interface (frame reads from HPS DDR) ─────────
    avm_address       : out   std_logic_vector(31 downto 0);
    avm_burstcount    : out   std_logic_vector(4 downto 0);
    avm_read          : out   std_logic;
    avm_readdata      : in    std_logic_vector(63 downto 0);
    avm_readdatavalid : in    std_logic;
    avm_waitrequest   : in    std_logic;

    -- ── Conduit: last completed frame, from cam_frame_writer ──────────
    frame_addr    : in    std_logic_vector(31 downto 0);
    frame_width   : in    std_logic_vector(15 downto 0);
    frame_height  : in    std_logic_vector(15 downto 0);
    frame_valid   : in    std_logic;

    -- ── Conduit: ADV7513 ──────────────────────────────────────────────
    hdmi_clk      : out   std_logic;
    hdmi_d        : out   std_logic_vector(23 downto 0);   -- R[23:16] G[15:8] B[7:0]
    hdmi_de       : out   std_logic;
    hdmi_hs       : out   std_logic;
    hdmi_vs       : out   std_logic;
    hdmi_int      : in    std_logic;
    hdmi_scl_oe   : out   std_logic;
    hdmi_sda_oe   : out   std_logic;
    hdmi_scl_in   : in    std_logic;
    hdmi_sda_in   : in    std_logic
  );
end entity hdmi_preview;

architecture rtl of hdmi_preview is

  constant ID_VALUE : std_logic_vector(31 downto 0) := x"48444D31";
  constant WORDS    : natural := H_ACTIVE / 8;       -- 64-bit words per row
  constant PAIRS    : natural := V_ACTIVE / 2;
  constant H_START  : natural := H_SYNC + H_BP;      -- first active column
  constant H_TOTAL  : natural := H_SYNC + H_BP + H_ACTIVE + H_FP;
  constant V_START  : natural := V_SYNC + V_BP;      -- first active line
  constant V_TOTAL  : natural := V_SYNC + V_BP + V_ACTIVE + V_FP;

  -- Line buffers: [bank][word], one for the GR row and one for the BG row
  type ram_t is array (0 to 2 * 256 - 1) of std_logic_vector(63 downto 0);
  signal ram_a, ram_b : ram_t := (others => (others => '0'));

  -- ── clk domain ─────────────────────────────────────────────────────────
  signal reg_ctrl   : std_logic_vector(2 downto 0) := (others => '0');
  signal reg_src_x  : signed(15 downto 0) := (others => '0');
  signal reg_src_y  : signed(15 downto 0) := (others => '0');
  signal reg_i2c    : std_logic_vector(1 downto 0) := (others => '0');
  signal frames     : unsigned(31 downto 0) := (others => '0');
  signal underflows : unsigned(31 downto 0) := (others => '0');
  signal scl_s1, scl_s2, sda_s1, sda_s2, int_s1, int_s2 : std_logic := '1';

  -- Toggles from pclk (request, frame, underflow), 3-flop synchronized
  signal req_s     : std_logic_vector(2 downto 0) := (others => '0');
  signal frame_s   : std_logic_vector(2 downto 0) := (others => '0');
  signal under_s   : std_logic_vector(2 downto 0) := (others => '0');

  -- Fetcher
  type fetch_t is (F_IDLE, F_ROW, F_ZERO_LEFT, F_READ, F_ZERO_RIGHT, F_DONE);
  signal fstate     : fetch_t := F_IDLE;
  signal f_base     : unsigned(31 downto 0) := (others => '0');
  signal f_width    : unsigned(15 downto 0) := (others => '0');
  signal f_height   : unsigned(15 downto 0) := (others => '0');
  signal f_valid    : std_logic := '0';
  signal f_src_x    : signed(15 downto 0) := (others => '0');
  signal f_src_y    : signed(15 downto 0) := (others => '0');
  signal f_pair     : unsigned(9 downto 0) := (others => '0');
  signal f_row_b    : std_logic := '0';                   -- fetching the BG row
  signal f_j0, f_j1 : unsigned(8 downto 0) := (others => '0');   -- valid words [j0, j1)
  signal f_addr     : unsigned(31 downto 0) := (others => '0');  -- next burst address
  signal f_req_left : unsigned(8 downto 0) := (others => '0');   -- words to request
  signal f_rsp_left : unsigned(8 downto 0) := (others => '0');   -- words to receive
  signal wptr       : unsigned(8 downto 0) := (others => '0');
  signal done_tgl   : std_logic := '0';
  signal rd         : std_logic := '0';                   -- avm_read
  signal rd_count   : unsigned(4 downto 0) := (others => '0');   -- avm_burstcount

  -- ── pclk domain ────────────────────────────────────────────────────────
  signal prst_s     : std_logic_vector(1 downto 0) := (others => '0');
  signal hc         : natural range 0 to H_TOTAL - 1 := 0;
  signal vc         : natural range 0 to V_TOTAL - 1 := 0;
  signal req_tgl    : std_logic := '0';
  signal req_pair   : unsigned(9 downto 0) := (others => '0');
  signal frame_tgl  : std_logic := '0';
  signal under_tgl  : std_logic := '0';
  signal done_ps    : std_logic_vector(2 downto 0) := (others => '0');
  signal fetched    : unsigned(9 downto 0) := (others => '0');   -- pairs ready this frame
  signal started    : std_logic := '0';      -- a frame start was requested since reset
  signal ctrl_p1, ctrl_p2 : std_logic_vector(2 downto 0) := (others => '0');
  signal valid_p1, valid_p2 : std_logic := '0';

  -- Pixel pipeline
  signal p1_de, p1_hs, p1_vs, p1_odd : std_logic := '0';
  signal p1_byte    : unsigned(2 downto 0) := (others => '0');
  signal p1_col     : natural range 0 to H_ACTIVE - 1 := 0;
  signal q_a, q_b   : std_logic_vector(63 downto 0) := (others => '0');

  attribute altera_attribute : string;
  attribute altera_attribute of req_s, frame_s, under_s, done_ps, prst_s : signal is
    "-name SYNCHRONIZER_IDENTIFICATION FORCED_IF_ASYNCHRONOUS";

  -- Byte i of a word, as a mux over constant slices (synthesizable for a
  -- variable i).
  function byte_of(w : std_logic_vector(63 downto 0); i : natural) return unsigned is
    variable b : unsigned(7 downto 0) := (others => '0');
  begin
    for k in 0 to 7 loop
      if k = i then
        b := unsigned(w(8 * k + 7 downto 8 * k));
      end if;
    end loop;
    return b;
  end function;

begin

  hdmi_clk    <= pclk;
  hdmi_scl_oe <= reg_i2c(0);
  hdmi_sda_oe <= reg_i2c(1);

  -- ════════════════════════════ clk domain ═══════════════════════════════

  -- ── Slave read logic ──────────────────────────────────────────────────
  process (avs_address, reg_ctrl, frame_valid, reg_src_x, reg_src_y, frames,
           underflows, reg_i2c, scl_s2, sda_s2, int_s2)
  begin
    avs_readdata <= (others => '0');
    case to_integer(unsigned(avs_address)) is
      when 0 => avs_readdata <= ID_VALUE;
      when 1 => avs_readdata(2 downto 0) <= reg_ctrl;
      when 2 => avs_readdata(0) <= frame_valid;
      when 3 => avs_readdata <= std_logic_vector(resize(reg_src_x, 32));
      when 4 => avs_readdata <= std_logic_vector(resize(reg_src_y, 32));
      when 5 => avs_readdata <= std_logic_vector(frames);
      when 6 => avs_readdata <= std_logic_vector(underflows);
      when 7 =>
        avs_readdata(1 downto 0) <= reg_i2c;
        avs_readdata(8)  <= scl_s2;
        avs_readdata(9)  <= sda_s2;
        avs_readdata(10) <= int_s2;
      when others => null;
    end case;
  end process;

  -- ── Slave write logic, pin synchronizers, counters ───────────────────
  process (clk, reset_n)
  begin
    if reset_n = '0' then
      reg_ctrl   <= (others => '0');
      reg_src_x  <= (others => '0');
      reg_src_y  <= (others => '0');
      reg_i2c    <= (others => '0');
      frames     <= (others => '0');
      underflows <= (others => '0');
    elsif rising_edge(clk) then
      if avs_write = '1' then
        case to_integer(unsigned(avs_address)) is
          when 1 => reg_ctrl  <= avs_writedata(2 downto 0);
          when 3 => reg_src_x <= signed(avs_writedata(15 downto 3)) & "000";
          when 4 => reg_src_y <= signed(avs_writedata(15 downto 1)) & '0';
          when 7 => reg_i2c   <= avs_writedata(1 downto 0);
          when others => null;
        end case;
      end if;
      scl_s1 <= hdmi_scl_in;
      scl_s2 <= scl_s1;
      sda_s1 <= hdmi_sda_in;
      sda_s2 <= sda_s1;
      int_s1 <= hdmi_int;
      int_s2 <= int_s1;
      frame_s <= frame_s(1 downto 0) & frame_tgl;
      under_s <= under_s(1 downto 0) & under_tgl;
      if frame_s(2) /= frame_s(1) then
        frames <= frames + 1;
      end if;
      if under_s(2) /= under_s(1) then
        underflows <= underflows + 1;
      end if;
    end if;
  end process;

  -- ── Line fetcher ──────────────────────────────────────────────────────
  -- Per row: zeros left of the frame, the frame's words (burst reads that
  -- never cross a 128-byte boundary, hence never 4 KB), zeros to the right.
  -- Rows outside the frame are all zeros.
  process (clk, reset_n)
    variable fy    : signed(16 downto 0);
    variable sxw   : signed(13 downto 0);      -- SRC_X in words
    variable lo    : signed(13 downto 0);
    variable hi    : signed(13 downto 0);
    variable burst : unsigned(4 downto 0);
    variable room  : unsigned(4 downto 0);
    variable bank  : unsigned(0 downto 0);
    variable we    : boolean;                  -- one line-buffer write per cycle
    variable wdata : std_logic_vector(63 downto 0);
  begin
    if reset_n = '0' then
      fstate         <= F_IDLE;
      req_s          <= (others => '0');
      done_tgl       <= '0';
      rd             <= '0';
      rd_count       <= (others => '0');
      f_req_left     <= (others => '0');
      f_rsp_left     <= (others => '0');
    elsif rising_edge(clk) then
      req_s <= req_s(1 downto 0) & req_tgl;
      bank  := f_pair(0 downto 0);
      we    := false;
      wdata := (others => '0');

      -- Read data returns in order: one word per readdatavalid.
      if avm_readdatavalid = '1' and f_rsp_left /= 0 then
        we         := true;
        wdata      := avm_readdata;
        f_rsp_left <= f_rsp_left - 1;
      end if;

      case fstate is

        when F_IDLE =>
          if req_s(2) /= req_s(1) then
            -- req_pair has been stable for two lines when the toggle arrives.
            f_pair  <= req_pair;
            f_row_b <= '0';
            if req_pair = 0 then
              -- New display frame: take the writer's latest frame.
              f_base   <= unsigned(frame_addr);
              f_width  <= unsigned(frame_width);
              f_height <= unsigned(frame_height);
              f_valid  <= frame_valid;
              f_src_x  <= reg_src_x;
              f_src_y  <= reg_src_y;
            end if;
            fstate <= F_ROW;
          end if;

        -- Which words of this row lie inside the frame, and where they are.
        when F_ROW =>
          fy  := resize(f_src_y, 17) + signed(resize(f_pair & f_row_b, 17));
          sxw := resize(f_src_x(15 downto 3), 14);
          lo  := -sxw;
          if lo < 0 then
            lo := (others => '0');
          end if;
          hi := signed(resize(f_width(15 downto 3), 14)) - sxw;
          if hi > WORDS then
            hi := to_signed(WORDS, 14);
          end if;
          if f_valid = '0' or fy < 0 or fy >= signed(resize(f_height, 17)) or hi <= lo then
            f_j0 <= (others => '0');
            f_j1 <= (others => '0');
          else
            f_j0   <= unsigned(lo(8 downto 0));
            f_j1   <= unsigned(hi(8 downto 0));
            f_addr <= f_base + resize(unsigned(fy(15 downto 0)) * f_width, 32) +
                      resize(unsigned(sxw + lo) & "000", 32);
          end if;
          wptr   <= (others => '0');
          fstate <= F_ZERO_LEFT;

        when F_ZERO_LEFT =>
          if wptr >= f_j0 then
            f_req_left <= f_j1 - f_j0;
            f_rsp_left <= f_j1 - f_j0;
            fstate     <= F_READ;
          else
            we := true;                                -- zero
          end if;

        when F_READ =>
          if rd = '1' then
            if avm_waitrequest = '0' then
              rd     <= '0';
              f_addr <= f_addr + shift_left(resize(rd_count, 32), 3);
            end if;
          elsif f_req_left /= 0 then
            room := to_unsigned(16, 5) - resize(f_addr(6 downto 3), 5);
            if f_req_left < room then
              burst := resize(f_req_left, 5);
            else
              burst := room;
            end if;
            rd         <= '1';
            rd_count   <= burst;
            f_req_left <= f_req_left - burst;
          elsif f_rsp_left = 0 and avm_readdatavalid = '0' then
            fstate <= F_ZERO_RIGHT;
          end if;

        when F_ZERO_RIGHT =>
          if wptr >= WORDS then
            if f_row_b = '0' then
              f_row_b <= '1';
              fstate  <= F_ROW;
            else
              fstate <= F_DONE;
            end if;
          else
            we := true;                                -- zero
          end if;

        when F_DONE =>
          done_tgl <= not done_tgl;
          fstate   <= F_IDLE;

      end case;

      if we then
        if f_row_b = '0' then
          ram_a(to_integer(bank & wptr(7 downto 0))) <= wdata;
        else
          ram_b(to_integer(bank & wptr(7 downto 0))) <= wdata;
        end if;
        wptr <= wptr + 1;
      end if;
    end if;
  end process;

  avm_address    <= std_logic_vector(f_addr);
  avm_read       <= rd;
  avm_burstcount <= std_logic_vector(rd_count);

  -- ════════════════════════════ pclk domain ══════════════════════════════

  -- ── Video timing and fetch requests ───────────────────────────────────
  process (pclk)
    variable row : natural range 0 to V_ACTIVE - 1;
  begin
    if rising_edge(pclk) then
      prst_s   <= prst_s(0) & reset_n;
      ctrl_p1  <= reg_ctrl;
      ctrl_p2  <= ctrl_p1;
      valid_p1 <= frame_valid;
      valid_p2 <= valid_p1;
      done_ps  <= done_ps(1 downto 0) & done_tgl;
      if done_ps(2) /= done_ps(1) then
        fetched <= fetched + 1;
      end if;

      if prst_s(1) = '0' then
        hc      <= 0;
        vc      <= 0;
        started <= '0';
      else
        if hc = H_TOTAL - 1 then
          hc <= 0;
          if vc = V_TOTAL - 1 then
            vc <= 0;
          else
            vc <= vc + 1;
          end if;
        else
          hc <= hc + 1;
        end if;

        if hc = 0 then
          if vc = V_START + V_ACTIVE then
            -- Vertical blank: ask for pair 0 of the next frame.
            req_pair  <= (others => '0');
            req_tgl   <= not req_tgl;
            fetched   <= (others => '0');
            frame_tgl <= not frame_tgl;
            started   <= '1';
          elsif started = '1' and vc >= V_START and vc < V_START + V_ACTIVE and (vc - V_START) mod 2 = 0 then
            -- Pair k goes on screen: it must be fetched; fetch pair k + 1.
            row := vc - V_START;
            if fetched < to_unsigned(row / 2 + 1, 10) then
              under_tgl <= not under_tgl;
            end if;
            if row / 2 + 1 < PAIRS then
              req_pair <= to_unsigned(row / 2 + 1, 10);
              req_tgl  <= not req_tgl;
            end if;
          end if;
        end if;
      end if;
    end if;
  end process;

  -- ── Pixel pipeline: counters -> RAM read -> pixel -> pins ─────────────
  process (pclk)
    variable active : boolean;
    variable row    : natural range 0 to V_ACTIVE - 1;
    variable col    : natural range 0 to H_ACTIVE - 1;
    variable cell   : natural range 0 to 7;
    variable g1, r, b, g2 : unsigned(7 downto 0);
    variable gsum   : unsigned(8 downto 0);
    variable rgb    : std_logic_vector(23 downto 0);
    variable bar    : natural range 0 to 7;
  begin
    if rising_edge(pclk) then
      -- Stage 1: address the line buffers.
      active := hc >= H_START and hc < H_START + H_ACTIVE and
                vc >= V_START and vc < V_START + V_ACTIVE;
      row := 0;
      col := 0;
      if active then
        row := vc - V_START;
        col := hc - H_START;
      end if;
      q_a <= ram_a(to_integer(to_unsigned(row / 2 mod 2, 1) & to_unsigned(col / 8, 8)));
      q_b <= ram_b(to_integer(to_unsigned(row / 2 mod 2, 1) & to_unsigned(col / 8, 8)));
      p1_de <= '0';
      if active then
        p1_de <= '1';
      end if;
      p1_hs <= '0';                                  -- 720p: positive syncs
      if hc < H_SYNC then
        p1_hs <= '1';
      end if;
      p1_vs <= '0';
      if vc < V_SYNC then
        p1_vs <= '1';
      end if;
      p1_odd  <= to_unsigned(row mod 2, 1)(0);
      p1_byte <= to_unsigned(col mod 8, 3);
      p1_col  <= col;

      -- Stage 2: the pixel.
      cell := to_integer(p1_byte(2 downto 1)) * 2;     -- G pixel of the 2x2 cell
      g1 := byte_of(q_a, cell);
      r  := byte_of(q_a, cell + 1);
      b  := byte_of(q_b, cell);
      g2 := byte_of(q_b, cell + 1);
      gsum := ('0' & g1) + ('0' & g2);
      if ctrl_p2(1) = '1' then                         -- grey: the raw value
        if p1_odd = '0' then
          r := byte_of(q_a, to_integer(p1_byte));
        else
          r := byte_of(q_b, to_integer(p1_byte));
        end if;
        rgb := std_logic_vector(r & r & r);
      else
        rgb := std_logic_vector(r & gsum(8 downto 1) & b);
      end if;
      if ctrl_p2(2) = '1' then                         -- color bars
        bar := 0;
        for k in 1 to 7 loop
          if p1_col >= k * H_ACTIVE / 8 then
            bar := k;
          end if;
        end loop;
        rgb := (others => '0');
        if bar = 0 or bar = 1 or bar = 4 or bar = 5 then rgb(23 downto 16) := x"FF"; end if;
        if bar <= 3 then rgb(15 downto 8) := x"FF"; end if;
        if bar mod 2 = 0 then rgb(7 downto 0) := x"FF"; end if;
      elsif ctrl_p2(0) = '0' or valid_p2 = '0' then
        rgb := (others => '0');
      end if;
      if p1_de = '0' then
        rgb := (others => '0');
      end if;
      hdmi_d  <= rgb;
      hdmi_de <= p1_de;
      hdmi_hs <= p1_hs;
      hdmi_vs <= p1_vs;
    end if;
  end process;

end architecture rtl;
