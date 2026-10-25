library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

--------------------------------------------------------------------------------
-- cam_tpg_writer — test-pattern generator + DDR frame writer for camd (M1).
--
-- Generates frames of the cvsoc test pattern, 8 pixels per clock, and writes
-- them into a ring of frame buffers in HPS DDR through an Avalon-MM burst
-- master (on the FPGA-to-HPS AXI bridge). camd polls FRAME_COUNT/STATUS and
-- streams the last completed buffer over GVSP.
--
-- Interfaces:
--   1. Avalon-MM slave (control/status registers) on the LW H2F bridge.
--   2. Avalon-MM master, 64-bit, bursts of up to 16 beats, with write
--      responses: a frame is reported done only after every burst of it has
--      been acknowledged by the HPS interconnect.
--
-- Register map (slave interface, 32-bit words):
--   Offset  Name            Access  Description
--   0x00    ID              R/O     0x43564331 ("CVC1")
--   0x04    CTRL            R/W     bit 0: enable (generate frames continuously)
--   0x08    STATUS          R/O     bit 0: busy (writing a frame)
--                                   bit 1: error (non-OKAY write response;
--                                          cleared when enable goes 0 -> 1)
--                                   bits [11:8]: index of last completed buffer
--   0x0C    FRAME_COUNT     R/O     frames completed since enable
--   0x10    WIDTH           R/W     pixels, multiple of 8
--   0x14    HEIGHT          R/W     lines
--   0x18    OFFSET_X        R/W     sensor x of the first pixel, multiple of 8
--   0x1C    OFFSET_Y        R/W     sensor y of the first line
--   0x20    PATTERN         R/W     0 off, 1 GreyHorizontalRampMoving, 2 ColorBar
--   0x24    FORMAT          R/W     0 BayerGR8, 1 Mono8
--   0x28    FRAME_INTERVAL  R/W     clock cycles between frame starts (0 = back to back)
--   0x2C    BUF_BASE        R/W     byte address of buffer 0 (128-byte aligned)
--   0x30    BUF_STRIDE      R/W     bytes between buffers (128-byte aligned)
--   0x34    BUF_COUNT       R/W     number of buffers, 1..16
--   0x38    SENSOR_WIDTH    R/O     SENSOR_WIDTH generic
--   0x3C    FRAME_CYCLES    R/O     clock cycles the last frame took to write
--
-- Configuration registers are sampled at the start of each frame.
--
-- Test pattern (must match tpg_pixel() in software/camd/source_sim.c and
-- client/tpg_model.py), with (x, y) in sensor coordinates and n = frame number:
--   GreyHorizontalRampMoving  (x + n) mod 256
--   ColorBar                  8 vertical bars, bar = x * 8 / SENSOR_WIDTH;
--                             BayerGR8 emits the CFA sample (even rows G R,
--                             odd rows B G), Mono8 emits the bar's luma.
--------------------------------------------------------------------------------
entity cam_tpg_writer is
  generic (
    SENSOR_WIDTH : positive := 2592
  );
  port (
    clk           : in    std_logic;
    reset_n       : in    std_logic;

    -- ── Avalon-MM Slave interface (control/status registers) ──────────
    avs_address   : in    std_logic_vector(3 downto 0);   -- word address [0..15]
    avs_read      : in    std_logic;
    avs_readdata  : out   std_logic_vector(31 downto 0);
    avs_write     : in    std_logic;
    avs_writedata : in    std_logic_vector(31 downto 0);

    -- ── Avalon-MM Master interface (frame writes into HPS DDR) ────────
    avm_address            : out   std_logic_vector(31 downto 0);
    avm_burstcount         : out   std_logic_vector(4 downto 0);
    avm_write              : out   std_logic;
    avm_writedata          : out   std_logic_vector(63 downto 0);
    avm_byteenable         : out   std_logic_vector(7 downto 0);
    avm_waitrequest        : in    std_logic;
    avm_writeresponsevalid : in    std_logic;
    avm_response           : in    std_logic_vector(1 downto 0)
  );
end entity cam_tpg_writer;

architecture rtl of cam_tpg_writer is

  constant ID_VALUE    : std_logic_vector(31 downto 0) := x"43564331";
  constant BURST_WORDS : natural := 16;   -- 16 x 8 bytes = 128 bytes

  constant PAT_RAMP     : unsigned(1 downto 0) := "01";
  constant PAT_COLORBAR : unsigned(1 downto 0) := "10";

  -- ── Color bars: white, yellow, cyan, green, magenta, red, blue, black ──
  type byte_array_t is array (natural range <>) of unsigned(7 downto 0);
  constant BAR_R : byte_array_t(0 to 7) := (x"FF", x"FF", x"00", x"00", x"FF", x"FF", x"00", x"00");
  constant BAR_G : byte_array_t(0 to 7) := (x"FF", x"FF", x"FF", x"FF", x"00", x"00", x"00", x"00");
  constant BAR_B : byte_array_t(0 to 7) := (x"FF", x"00", x"FF", x"00", x"FF", x"00", x"FF", x"00");

  function bar_luma(i : natural) return unsigned is
  begin
    return to_unsigned((77 * to_integer(BAR_R(i)) + 150 * to_integer(BAR_G(i)) +
                        29 * to_integer(BAR_B(i))) / 256, 8);
  end function;

  -- bar(x) = floor(x * 8 / W) = number of k in 1..7 with x >= ceil(k * W / 8).
  type bound_array_t is array (1 to 7) of natural;
  function bar_bounds return bound_array_t is
    variable b : bound_array_t;
  begin
    for k in 1 to 7 loop
      b(k) := (k * SENSOR_WIDTH + 7) / 8;
    end loop;
    return b;
  end function;
  constant BOUNDS : bound_array_t := bar_bounds;

  function bar_of(x : unsigned(15 downto 0)) return natural is
    variable bar : natural range 0 to 7 := 0;
  begin
    for k in 1 to 7 loop
      if to_integer(x) >= BOUNDS(k) then
        bar := k;
      end if;
    end loop;
    return bar;
  end function;

  -- FSM states
  type state_t is (S_IDLE, S_START, S_BURST, S_WAIT_RESP);
  signal state : state_t := S_IDLE;

  -- Control/status registers
  signal reg_enable    : std_logic := '0';
  signal reg_width     : unsigned(15 downto 0) := to_unsigned(SENSOR_WIDTH, 16);
  signal reg_height    : unsigned(15 downto 0) := (others => '0');
  signal reg_offset_x  : unsigned(15 downto 0) := (others => '0');
  signal reg_offset_y  : unsigned(15 downto 0) := (others => '0');
  signal reg_pattern   : unsigned(1 downto 0)  := PAT_RAMP;
  signal reg_mono      : std_logic := '0';
  signal reg_interval  : unsigned(31 downto 0) := (others => '0');
  signal reg_buf_base  : unsigned(31 downto 0) := (others => '0');
  signal reg_stride    : unsigned(31 downto 0) := (others => '0');
  signal reg_buf_count : unsigned(4 downto 0)  := to_unsigned(1, 5);

  signal frame_count   : unsigned(31 downto 0) := (others => '0');
  signal last_buf      : unsigned(3 downto 0)  := (others => '0');
  signal resp_error    : std_logic := '0';
  signal frame_cycles  : unsigned(31 downto 0) := (others => '0');
  signal enable_d      : std_logic := '0';
  signal restart       : std_logic := '0';   -- enable rose; reset the ring when idle

  -- Per-frame configuration, sampled at frame start
  signal cfg_words_per_row : unsigned(12 downto 0) := (others => '0');
  signal cfg_height        : unsigned(15 downto 0) := (others => '0');
  signal cfg_offset_x      : unsigned(15 downto 0) := (others => '0');
  signal cfg_offset_y      : unsigned(15 downto 0) := (others => '0');
  signal cfg_pattern       : unsigned(1 downto 0)  := (others => '0');
  signal cfg_mono          : std_logic := '0';
  signal cfg_n             : unsigned(7 downto 0)  := (others => '0');

  -- Frame position
  signal buf_idx      : unsigned(3 downto 0)  := (others => '0');
  signal buf_addr     : unsigned(31 downto 0) := (others => '0');
  signal burst_addr   : unsigned(31 downto 0) := (others => '0');
  signal col_word     : unsigned(12 downto 0) := (others => '0');
  signal row          : unsigned(15 downto 0) := (others => '0');
  signal words_left   : unsigned(25 downto 0) := (others => '0');
  signal beats_left   : unsigned(4 downto 0)  := (others => '0');
  signal bursts_out   : unsigned(15 downto 0) := (others => '0');  -- issued - acknowledged
  signal interval_cnt : unsigned(31 downto 0) := (others => '0');
  signal cycle_cnt    : unsigned(31 downto 0) := (others => '0');

  signal pixel_word   : std_logic_vector(63 downto 0);

begin

  avm_byteenable <= (others => '1');
  avm_address    <= std_logic_vector(burst_addr);
  avm_writedata  <= pixel_word;

  -- ── Pixel generator: 8 pixels of the current word ─────────────────────
  process (col_word, row, cfg_offset_x, cfg_offset_y, cfg_pattern, cfg_mono, cfg_n)
    variable x_base : unsigned(15 downto 0);
    variable x      : unsigned(15 downto 0);
    variable y      : unsigned(15 downto 0);
    variable bar    : natural range 0 to 7;
    variable pix    : unsigned(7 downto 0);
  begin
    x_base := cfg_offset_x + (col_word & "000");
    y      := cfg_offset_y + row;
    for k in 0 to 7 loop
      x := x_base + to_unsigned(k, 16);
      pix := (others => '0');
      if cfg_pattern = PAT_RAMP then
        pix := x(7 downto 0) + cfg_n;
      elsif cfg_pattern = PAT_COLORBAR then
        bar := bar_of(x);
        if cfg_mono = '1' then
          pix := bar_luma(bar);
        elsif y(0) = '0' then
          if x(0) = '0' then pix := BAR_G(bar); else pix := BAR_R(bar); end if;   -- G R
        else
          if x(0) = '0' then pix := BAR_B(bar); else pix := BAR_G(bar); end if;   -- B G
        end if;
      end if;
      pixel_word(8 * k + 7 downto 8 * k) <= std_logic_vector(pix);
    end loop;
  end process;

  -- ── Slave read logic ──────────────────────────────────────────────────
  process (avs_address, reg_enable, state, resp_error, last_buf, frame_count,
           reg_width, reg_height, reg_offset_x, reg_offset_y, reg_pattern, reg_mono,
           reg_interval, reg_buf_base, reg_stride, reg_buf_count, frame_cycles)
  begin
    avs_readdata <= (others => '0');
    case to_integer(unsigned(avs_address)) is
      when 0  => avs_readdata <= ID_VALUE;
      when 1  => avs_readdata(0) <= reg_enable;
      when 2  =>
        if state /= S_IDLE then
          avs_readdata(0) <= '1';
        end if;
        avs_readdata(1) <= resp_error;
        avs_readdata(11 downto 8) <= std_logic_vector(last_buf);
      when 3  => avs_readdata <= std_logic_vector(frame_count);
      when 4  => avs_readdata(15 downto 0) <= std_logic_vector(reg_width);
      when 5  => avs_readdata(15 downto 0) <= std_logic_vector(reg_height);
      when 6  => avs_readdata(15 downto 0) <= std_logic_vector(reg_offset_x);
      when 7  => avs_readdata(15 downto 0) <= std_logic_vector(reg_offset_y);
      when 8  => avs_readdata(1 downto 0)  <= std_logic_vector(reg_pattern);
      when 9  => avs_readdata(0) <= reg_mono;
      when 10 => avs_readdata <= std_logic_vector(reg_interval);
      when 11 => avs_readdata <= std_logic_vector(reg_buf_base);
      when 12 => avs_readdata <= std_logic_vector(reg_stride);
      when 13 => avs_readdata(4 downto 0) <= std_logic_vector(reg_buf_count);
      when 14 => avs_readdata <= std_logic_vector(to_unsigned(SENSOR_WIDTH, 32));
      when 15 => avs_readdata <= std_logic_vector(frame_cycles);
      when others => null;
    end case;
  end process;

  -- ── Slave write logic ─────────────────────────────────────────────────
  process (clk, reset_n)
  begin
    if reset_n = '0' then
      reg_enable    <= '0';
      reg_width     <= to_unsigned(SENSOR_WIDTH, 16);
      reg_height    <= (others => '0');
      reg_offset_x  <= (others => '0');
      reg_offset_y  <= (others => '0');
      reg_pattern   <= PAT_RAMP;
      reg_mono      <= '0';
      reg_interval  <= (others => '0');
      reg_buf_base  <= (others => '0');
      reg_stride    <= (others => '0');
      reg_buf_count <= to_unsigned(1, 5);
    elsif rising_edge(clk) then
      if avs_write = '1' then
        case to_integer(unsigned(avs_address)) is
          when 1  => reg_enable   <= avs_writedata(0);
          when 4  => reg_width    <= unsigned(avs_writedata(15 downto 3)) & "000";
          when 5  => reg_height   <= unsigned(avs_writedata(15 downto 0));
          when 6  => reg_offset_x <= unsigned(avs_writedata(15 downto 3)) & "000";
          when 7  => reg_offset_y <= unsigned(avs_writedata(15 downto 0));
          when 8  => reg_pattern  <= unsigned(avs_writedata(1 downto 0));
          when 9  => reg_mono     <= avs_writedata(0);
          when 10 => reg_interval <= unsigned(avs_writedata);
          when 11 => reg_buf_base <= unsigned(avs_writedata(31 downto 7)) & "0000000";
          when 12 => reg_stride   <= unsigned(avs_writedata(31 downto 7)) & "0000000";
          when 13 =>
            if unsigned(avs_writedata(4 downto 0)) = 0 then
              reg_buf_count <= to_unsigned(1, 5);
            elsif unsigned(avs_writedata(4 downto 0)) > 16 then
              reg_buf_count <= to_unsigned(16, 5);
            else
              reg_buf_count <= unsigned(avs_writedata(4 downto 0));
            end if;
          when others => null;
        end case;
      end if;
    end if;
  end process;

  -- ── Master FSM ────────────────────────────────────────────────────────
  process (clk, reset_n)
    variable burst : unsigned(4 downto 0);
  begin
    if reset_n = '0' then
      state          <= S_IDLE;
      enable_d       <= '0';
      restart        <= '0';
      frame_count    <= (others => '0');
      last_buf       <= (others => '0');
      resp_error     <= '0';
      frame_cycles   <= (others => '0');
      buf_idx        <= (others => '0');
      buf_addr       <= (others => '0');
      burst_addr     <= (others => '0');
      col_word       <= (others => '0');
      row            <= (others => '0');
      words_left     <= (others => '0');
      beats_left     <= (others => '0');
      bursts_out     <= (others => '0');
      interval_cnt   <= (others => '0');
      cycle_cnt      <= (others => '0');
      avm_write      <= '0';
      avm_burstcount <= (others => '0');

    elsif rising_edge(clk) then
      enable_d <= reg_enable;
      if reg_enable = '1' and enable_d = '0' then
        restart <= '1';
      end if;
      if interval_cnt /= x"FFFFFFFF" then
        interval_cnt <= interval_cnt + 1;
      end if;
      cycle_cnt <= cycle_cnt + 1;

      -- Write responses can arrive in any state: one per burst.
      if avm_writeresponsevalid = '1' then
        if avm_response /= "00" then
          resp_error <= '1';
        end if;
      end if;

      case state is

        -- ── IDLE: wait for enable and the frame interval ─────────────────
        when S_IDLE =>
          if restart = '1' or (reg_enable = '1' and enable_d = '0') then
            -- Enable rose (now, or while a frame was in flight): restart the
            -- ring and the frame count before the next frame starts.
            restart     <= '0';
            frame_count <= (others => '0');
            buf_idx     <= (others => '0');
            buf_addr    <= reg_buf_base;
            resp_error  <= '0';
            interval_cnt <= (others => '1');
          elsif reg_enable = '1' and interval_cnt >= reg_interval and
                reg_height /= 0 and reg_width /= 0 then
            cfg_words_per_row <= reg_width(15 downto 3);
            cfg_height        <= reg_height;
            cfg_offset_x      <= reg_offset_x;
            cfg_offset_y      <= reg_offset_y;
            cfg_pattern       <= reg_pattern;
            cfg_mono          <= reg_mono;
            cfg_n             <= frame_count(7 downto 0);
            interval_cnt      <= (others => '0');
            cycle_cnt         <= (others => '0');
            state             <= S_START;
          end if;

        -- ── START: size the frame (one multiply) and point at the buffer ─
        when S_START =>
          words_left <= resize(cfg_words_per_row * cfg_height, 26);
          burst_addr <= buf_addr;
          col_word   <= (others => '0');
          row        <= (others => '0');
          beats_left <= (others => '0');
          state      <= S_BURST;

        -- ── BURST: issue 16-beat bursts until the frame is written ───────
        when S_BURST =>
          if beats_left = 0 then
            -- Begin a burst; address and burstcount hold for all its beats.
            if words_left = 0 then
              avm_write <= '0';
              state     <= S_WAIT_RESP;
            else
              if words_left >= BURST_WORDS then
                burst := to_unsigned(BURST_WORDS, 5);
              else
                burst := resize(words_left, 5);
              end if;
              avm_burstcount <= std_logic_vector(burst);
              beats_left     <= burst;
              avm_write      <= '1';
              bursts_out     <= bursts_out + 1;
            end if;
          elsif avm_waitrequest = '0' then
            -- Beat accepted: advance to the next pixel word.
            words_left <= words_left - 1;
            beats_left <= beats_left - 1;
            if col_word = cfg_words_per_row - 1 then
              col_word <= (others => '0');
              row      <= row + 1;
            else
              col_word <= col_word + 1;
            end if;
            if beats_left = 1 then
              avm_write  <= '0';
              burst_addr <= burst_addr + BURST_WORDS * 8;
            end if;
          end if;
          if avm_writeresponsevalid = '1' then
            if beats_left = 0 and words_left /= 0 then
              bursts_out <= bursts_out;     -- +1 issued, -1 acknowledged
            else
              bursts_out <= bursts_out - 1;
            end if;
          end if;

        -- ── WAIT_RESP: every burst acknowledged -> frame is in DDR ───────
        when S_WAIT_RESP =>
          if avm_writeresponsevalid = '1' then
            bursts_out <= bursts_out - 1;
          end if;
          if bursts_out = 0 or (bursts_out = 1 and avm_writeresponsevalid = '1') then
            frame_count  <= frame_count + 1;
            last_buf     <= buf_idx;
            frame_cycles <= cycle_cnt;
            if buf_idx + 1 >= reg_buf_count then
              buf_idx  <= (others => '0');
              buf_addr <= reg_buf_base;
            else
              buf_idx  <= buf_idx + 1;
              buf_addr <= buf_addr + reg_stride;
            end if;
            state <= S_IDLE;
          end if;

      end case;
    end if;
  end process;

end architecture rtl;
