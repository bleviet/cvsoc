library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.cam_pkg.all;

--------------------------------------------------------------------------------
-- cam_frame_writer — camera frame source and DDR frame writer for camd.
--
-- Writes frames, 8 pixels per 64-bit word, into a ring of frame buffers in
-- HPS DDR through an Avalon-MM burst master (on the FPGA-to-HPS AXI bridge).
-- camd polls FRAME_COUNT/STATUS and streams the last completed buffer.
--
-- Frame sources, selected by PATTERN:
--   0       the D5M sensor: d5m_capture (PIXCLK domain) -> async_fifo -> here.
--           A frame starts at a TAG_SOF word and is published only if it has
--           exactly WIDTH x HEIGHT pixels; other frames count in BAD_FRAMES.
--   1, 2    the test-pattern generator.
--
-- The sensor itself is configured by camd over I2C, bit-banged through the
-- I2C register (ADR-009); this block only drives its clock and control pins.
--
-- Interfaces:
--   1. Avalon-MM slave (control/status registers) on the LW H2F bridge.
--   2. Avalon-MM master, 64-bit, bursts of up to 16 beats, with write
--      responses: a frame is reported done only after every burst of it has
--      been acknowledged by the HPS interconnect. A burst starts only when all
--      its data is available, so `write` stays high for every beat.
--   3. Conduit to the D5M pins (GPIO header JP1).
--
-- Register map (slave interface, 32-bit words):
--   Offset  Name            Access  Description
--   0x00    ID              R/O     0x43564332 ("CVC2")
--   0x04    CTRL            R/W     bit 0: enable (write frames continuously)
--   0x08    STATUS          R/O     bit 0: busy (writing a frame)
--                                   bit 1: error (non-OKAY write response;
--                                          cleared when enable goes 0 -> 1)
--                                   bits [11:8]: index of last completed buffer
--   0x0C    FRAME_COUNT     R/O     frames completed since enable
--   0x10    WIDTH           R/W     pixels, multiple of 8
--   0x14    HEIGHT          R/W     lines
--   0x18    OFFSET_X        R/W     test pattern: sensor x of the first pixel, multiple of 8
--   0x1C    OFFSET_Y        R/W     test pattern: sensor y of the first line
--   0x20    PATTERN         R/W     0 sensor, 1 GreyHorizontalRampMoving, 2 ColorBar
--   0x24    FORMAT          R/W     test pattern: 0 BayerGR8, 1 Mono8
--   0x28    FRAME_INTERVAL  R/W     clock cycles between frame starts (0 = no limit)
--   0x2C    BUF_BASE        R/W     byte address of buffer 0 (128-byte aligned)
--   0x30    BUF_STRIDE      R/W     bytes between buffers (128-byte aligned)
--   0x34    BUF_COUNT       R/W     number of buffers, 1..16
--   0x38    SENSOR_WIDTH    R/O     SENSOR_WIDTH generic
--   0x3C    FRAME_CYCLES    R/O     clock cycles the last frame took to write
--   0x40    SENSOR_CTRL     R/W     bit 0: RESETn pin (1 = sensor running)
--                                   bit 1: XCLKIN on (25 MHz = clk / 2)
--                                   bit 2: TRIGGER pin
--                                   bit 3: capture enable (d5m_capture)
--   0x44    I2C             R/W     bit 0: pull SCLK low, bit 1: pull SDATA low
--                                   R/O bit 8: SCLK pin, bit 9: SDATA pin,
--                                   bit 10: STROBE pin
--   0x48    CAP_FRAMES      R/O     sensor frames captured (trailers seen)
--   0x4C    CAP_SIZE        R/O     last sensor frame: [11:0] lines,
--                                   [27:16] pixels in its last line
--   0x50    CAP_LINE_PERIOD R/O     last sensor frame: PIXCLK cycles per line
--   0x54    CAP_BITS        R/O     last sensor frame: [11:0] OR, [27:16] AND
--                                   of all 12-bit pixels
--   0x58    CAP_FLAGS       R/O     last sensor frame: bit 0 FIFO overflow,
--                                   bit 1 irregular line lengths
--   0x5C    CAP_OVERFLOWS   R/O     sensor frames with a FIFO overflow
--   0x60    FRAME_PERIOD    R/O     clock cycles between the last two sensor frames
--   0x64    BAD_FRAMES      R/O     sensor frames dropped for a size mismatch
--
-- Counters at 0x48, 0x5C and 0x64 run from reset; camd uses deltas.
-- Configuration registers are sampled at the start of each frame.
--
-- Test pattern (must match tpg_pixel() in software/camd/source_sim.c and
-- client/tpg_model.py), with (x, y) in sensor coordinates and n = frame number:
--   GreyHorizontalRampMoving  (x + n) mod 256
--   ColorBar                  8 vertical bars, bar = x * 8 / SENSOR_WIDTH;
--                             BayerGR8 emits the CFA sample (even rows G R,
--                             odd rows B G), Mono8 emits the bar's luma.
--------------------------------------------------------------------------------
entity cam_frame_writer is
  generic (
    SENSOR_WIDTH : positive := 2592
  );
  port (
    clk           : in    std_logic;
    reset_n       : in    std_logic;

    -- ── Avalon-MM Slave interface (control/status registers) ──────────
    avs_address   : in    std_logic_vector(4 downto 0);   -- word address [0..31]
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
    avm_response           : in    std_logic_vector(1 downto 0);

    -- ── Conduit: TRDB-D5M ─────────────────────────────────────────────
    d5m_pixclk    : in    std_logic;
    d5m_d         : in    std_logic_vector(11 downto 0);
    d5m_fval      : in    std_logic;
    d5m_lval      : in    std_logic;
    d5m_strobe    : in    std_logic;
    d5m_xclkin    : out   std_logic;
    d5m_reset_n   : out   std_logic;
    d5m_trigger   : out   std_logic;
    d5m_scl_oe    : out   std_logic;      -- 1: pull SCLK low (open drain)
    d5m_sda_oe    : out   std_logic;      -- 1: pull SDATA low (open drain)
    d5m_scl_in    : in    std_logic;
    d5m_sda_in    : in    std_logic
  );
end entity cam_frame_writer;

architecture rtl of cam_frame_writer is

  constant ID_VALUE    : std_logic_vector(31 downto 0) := x"43564332";
  constant BURST_WORDS : natural := 16;   -- 16 x 8 bytes = 128 bytes

  constant PAT_SENSOR   : unsigned(1 downto 0) := "00";
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
  type state_t is (S_IDLE, S_START, S_BURST, S_WAIT_RESP, S_CHECK);
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
  signal reg_sensor    : std_logic_vector(3 downto 0) := (others => '0');
  signal reg_i2c       : std_logic_vector(1 downto 0) := (others => '0');

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
  signal cfg_sensor        : std_logic := '0';

  -- Frame position
  signal buf_idx      : unsigned(3 downto 0)  := (others => '0');
  signal buf_addr     : unsigned(31 downto 0) := (others => '0');
  signal burst_addr   : unsigned(31 downto 0) := (others => '0');
  signal words_left   : unsigned(25 downto 0) := (others => '0');
  signal first_word   : std_logic := '0';
  signal frame_bad    : std_logic := '0';
  signal beats_left   : unsigned(4 downto 0)  := (others => '0');
  signal bursts_out   : unsigned(15 downto 0) := (others => '0');  -- issued - acknowledged
  signal interval_cnt : unsigned(31 downto 0) := (others => '0');
  signal cycle_cnt    : unsigned(31 downto 0) := (others => '0');

  -- Test-pattern generator: one registered word ahead of the writer
  signal gen_restart : std_logic := '0';
  signal gen_col     : unsigned(12 downto 0) := (others => '0');
  signal gen_row     : unsigned(15 downto 0) := (others => '0');
  signal gen_valid   : std_logic := '0';
  signal gen_data    : std_logic_vector(63 downto 0) := (others => '0');
  signal gen_word    : std_logic_vector(63 downto 0);

  -- Sensor path
  signal cap_wr_en    : std_logic;
  signal cap_wr_data  : std_logic_vector(CAP_WORD_WIDTH - 1 downto 0);
  signal cap_wr_full  : std_logic;
  signal fifo_rd_en   : std_logic;
  signal fifo_data    : std_logic_vector(CAP_WORD_WIDTH - 1 downto 0);
  signal fifo_empty   : std_logic;
  signal fifo_count   : unsigned(9 downto 0);
  signal head_tag     : std_logic_vector(1 downto 0);
  signal head_pixels  : std_logic;   -- head word may be written as frame data
  signal start_sensor : std_logic;   -- IDLE: start a sensor frame at the head SOF
  signal drain        : std_logic;   -- pop the head word without writing it
  signal beat         : std_logic;   -- a burst beat is accepted this cycle

  -- Sensor statistics (from trailers)
  signal cap_frames      : unsigned(31 downto 0) := (others => '0');
  signal cap_lines       : std_logic_vector(11 downto 0) := (others => '0');
  signal cap_last_px     : std_logic_vector(11 downto 0) := (others => '0');
  signal cap_line_period : std_logic_vector(15 downto 0) := (others => '0');
  signal cap_or          : std_logic_vector(11 downto 0) := (others => '0');
  signal cap_and         : std_logic_vector(11 downto 0) := (others => '0');
  signal cap_flags       : std_logic_vector(1 downto 0)  := (others => '0');
  signal cap_overflows   : unsigned(31 downto 0) := (others => '0');
  signal frame_period    : unsigned(31 downto 0) := (others => '0');
  signal period_cnt      : unsigned(31 downto 0) := (others => '0');
  signal bad_frames      : unsigned(31 downto 0) := (others => '0');

  -- Pins
  signal xclk        : std_logic := '0';
  signal scl_s1, scl_s2, sda_s1, sda_s2, strobe_s1, strobe_s2 : std_logic := '1';

begin

  avm_byteenable <= (others => '1');
  avm_address    <= std_logic_vector(burst_addr);

  -- ── D5M pins ──────────────────────────────────────────────────────────
  d5m_reset_n <= reg_sensor(0);
  d5m_trigger <= reg_sensor(2);
  d5m_xclkin  <= xclk;
  d5m_scl_oe  <= reg_i2c(0);
  d5m_sda_oe  <= reg_i2c(1);

  process (clk)
  begin
    if rising_edge(clk) then
      if reg_sensor(1) = '1' then
        xclk <= not xclk;              -- 25 MHz from the 50 MHz fabric clock
      else
        xclk <= '0';
      end if;
      scl_s1    <= d5m_scl_in;
      scl_s2    <= scl_s1;
      sda_s1    <= d5m_sda_in;
      sda_s2    <= sda_s1;
      strobe_s1 <= d5m_strobe;
      strobe_s2 <= strobe_s1;
    end if;
  end process;

  -- ── Sensor capture (PIXCLK domain) and clock-domain crossing ─────────
  capture_inst : entity work.d5m_capture
    port map (
      pixclk  => d5m_pixclk,
      d       => d5m_d,
      fval    => d5m_fval,
      lval    => d5m_lval,
      enable  => reg_sensor(3),
      wr_en   => cap_wr_en,
      wr_data => cap_wr_data,
      wr_full => cap_wr_full
    );

  fifo_inst : entity work.async_fifo
    generic map (WIDTH => CAP_WORD_WIDTH, ADDR_WIDTH => 9)
    port map (
      wr_clk   => d5m_pixclk,
      wr_en    => cap_wr_en,
      wr_data  => cap_wr_data,
      wr_full  => cap_wr_full,
      rd_clk   => clk,
      rd_en    => fifo_rd_en,
      rd_data  => fifo_data,
      rd_empty => fifo_empty,
      rd_count => fifo_count
    );

  head_tag <= fifo_data(CAP_WORD_WIDTH - 1 downto CAP_WORD_WIDTH - 2);

  -- Pixel words belong to the frame; a SOF only as its first word. Anything
  -- else at the head during a frame means the sensor frame was too short.
  head_pixels <= '1' when fifo_empty = '0' and
                          (head_tag = TAG_PIXELS or (head_tag = TAG_SOF and first_word = '1'))
                 else '0';

  start_sensor <= '1' when state = S_IDLE and restart = '0' and
                           not (reg_enable = '1' and enable_d = '0') and
                           reg_enable = '1' and reg_pattern = PAT_SENSOR and
                           interval_cnt >= reg_interval and
                           reg_height /= 0 and reg_width /= 0 and
                           fifo_empty = '0' and head_tag = TAG_SOF
                  else '0';

  beat <= '1' when state = S_BURST and beats_left /= 0 and avm_waitrequest = '0' else '0';

  -- The FIFO is drained whenever no sensor frame is being written: while
  -- idle (except the SOF a frame starts at) and during test-pattern frames.
  -- Trailers are always consumed here, never as frame data.
  drain <= '1' when fifo_empty = '0' and
                    ((state = S_IDLE and start_sensor = '0') or
                     (state /= S_IDLE and cfg_sensor = '0'))
           else '0';

  fifo_rd_en <= drain or (beat and cfg_sensor and head_pixels);

  avm_writedata <= gen_data when cfg_sensor = '0' else
                   fifo_data(63 downto 0) when head_pixels = '1' else
                   (others => '0');      -- short frame: pad the burst

  -- ── Test-pattern generator: 8 pixels at (gen_col, gen_row) ────────────
  process (gen_col, gen_row, cfg_offset_x, cfg_offset_y, cfg_pattern, cfg_mono, cfg_n)
    variable x_base : unsigned(15 downto 0);
    variable x      : unsigned(15 downto 0);
    variable y      : unsigned(15 downto 0);
    variable bar    : natural range 0 to 7;
    variable pix    : unsigned(7 downto 0);
  begin
    x_base := cfg_offset_x + (gen_col & "000");
    y      := cfg_offset_y + gen_row;
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
      gen_word(8 * k + 7 downto 8 * k) <= std_logic_vector(pix);
    end loop;
  end process;

  -- The generator keeps one word ready; a beat consumes it and the next one
  -- is registered in the same cycle, so bursts never wait for data.
  process (clk)
  begin
    if rising_edge(clk) then
      if gen_restart = '1' then
        gen_col   <= (others => '0');
        gen_row   <= (others => '0');
        gen_valid <= '0';
      elsif gen_valid = '0' or (beat = '1' and cfg_sensor = '0') then
        gen_data  <= gen_word;
        gen_valid <= '1';
        if gen_col = cfg_words_per_row - 1 then
          gen_col <= (others => '0');
          gen_row <= gen_row + 1;
        else
          gen_col <= gen_col + 1;
        end if;
      end if;
    end if;
  end process;

  -- ── Slave read logic ──────────────────────────────────────────────────
  process (avs_address, reg_enable, state, resp_error, last_buf, frame_count,
           reg_width, reg_height, reg_offset_x, reg_offset_y, reg_pattern, reg_mono,
           reg_interval, reg_buf_base, reg_stride, reg_buf_count, frame_cycles,
           reg_sensor, reg_i2c, scl_s2, sda_s2, strobe_s2, cap_frames, cap_lines,
           cap_last_px, cap_line_period, cap_or, cap_and, cap_flags, cap_overflows,
           frame_period, bad_frames)
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
      when 16 => avs_readdata(3 downto 0) <= reg_sensor;
      when 17 =>
        avs_readdata(1 downto 0) <= reg_i2c;
        avs_readdata(8)  <= scl_s2;
        avs_readdata(9)  <= sda_s2;
        avs_readdata(10) <= strobe_s2;
      when 18 => avs_readdata <= std_logic_vector(cap_frames);
      when 19 =>
        avs_readdata(11 downto 0)  <= cap_lines;
        avs_readdata(27 downto 16) <= cap_last_px;
      when 20 => avs_readdata(15 downto 0) <= cap_line_period;
      when 21 =>
        avs_readdata(11 downto 0)  <= cap_or;
        avs_readdata(27 downto 16) <= cap_and;
      when 22 => avs_readdata(1 downto 0) <= cap_flags;
      when 23 => avs_readdata <= std_logic_vector(cap_overflows);
      when 24 => avs_readdata <= std_logic_vector(frame_period);
      when 25 => avs_readdata <= std_logic_vector(bad_frames);
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
      reg_sensor    <= (others => '0');
      reg_i2c       <= (others => '0');
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
          when 16 => reg_sensor <= avs_writedata(3 downto 0);
          when 17 => reg_i2c    <= avs_writedata(1 downto 0);
          when others => null;
        end case;
      end if;
    end if;
  end process;

  -- ── Sensor statistics: one trailer per captured frame ─────────────────
  process (clk, reset_n)
  begin
    if reset_n = '0' then
      cap_frames      <= (others => '0');
      cap_lines       <= (others => '0');
      cap_last_px     <= (others => '0');
      cap_line_period <= (others => '0');
      cap_or          <= (others => '0');
      cap_and         <= (others => '0');
      cap_flags       <= (others => '0');
      cap_overflows   <= (others => '0');
      frame_period    <= (others => '0');
      period_cnt      <= (others => '0');
    elsif rising_edge(clk) then
      if period_cnt /= x"FFFFFFFF" then
        period_cnt <= period_cnt + 1;
      end if;
      if drain = '1' and head_tag = TAG_TRAILER then
        cap_frames      <= cap_frames + 1;
        cap_lines       <= fifo_data(11 downto 0);
        cap_last_px     <= fifo_data(23 downto 12);
        cap_line_period <= fifo_data(39 downto 24);
        cap_or          <= fifo_data(51 downto 40);
        cap_and         <= fifo_data(63 downto 52);
        cap_flags       <= fifo_data(FLAG_IRREGULAR) & fifo_data(FLAG_OVERFLOW);
        if fifo_data(FLAG_OVERFLOW) = '1' then
          cap_overflows <= cap_overflows + 1;
        end if;
        frame_period <= period_cnt;
        period_cnt   <= (others => '0');
      end if;
    end if;
  end process;

  -- ── Master FSM ────────────────────────────────────────────────────────
  process (clk, reset_n)
    variable burst : unsigned(4 downto 0);
    variable issue : boolean;   -- S_BURST starts a burst this cycle

    -- The frame is in DDR: report it and move to the next buffer.
    procedure publish_frame is
    begin
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
    end procedure;
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
      words_left     <= (others => '0');
      first_word     <= '0';
      frame_bad      <= '0';
      beats_left     <= (others => '0');
      bursts_out     <= (others => '0');
      interval_cnt   <= (others => '0');
      cycle_cnt      <= (others => '0');
      cfg_sensor     <= '0';
      gen_restart    <= '0';
      bad_frames     <= (others => '0');
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
      cycle_cnt   <= cycle_cnt + 1;
      gen_restart <= '0';

      -- Next burst: up to 16 beats, started only once all its data is
      -- available (sensor: in the FIFO; test pattern: always one cycle away).
      if words_left >= BURST_WORDS then
        burst := to_unsigned(BURST_WORDS, 5);
      else
        burst := resize(words_left, 5);
      end if;
      issue := state = S_BURST and beats_left = 0 and words_left /= 0 and
               frame_bad = '0' and
               ((cfg_sensor = '1' and reg_enable = '1' and fifo_count >= burst) or
                (cfg_sensor = '0' and gen_valid = '1'));

      -- Write responses can arrive in any state: one per burst.
      if avm_writeresponsevalid = '1' then
        if avm_response /= "00" then
          resp_error <= '1';
        end if;
      end if;

      case state is

        -- ── IDLE: wait for enable, the frame interval and (sensor) a SOF ─
        when S_IDLE =>
          if restart = '1' or (reg_enable = '1' and enable_d = '0') then
            -- Enable rose (now, or while a frame was in flight): restart the
            -- ring and the frame count before the next frame starts.
            restart      <= '0';
            frame_count  <= (others => '0');
            buf_idx      <= (others => '0');
            buf_addr     <= reg_buf_base;
            resp_error   <= '0';
            interval_cnt <= (others => '1');
          elsif start_sensor = '1' or
                (reg_enable = '1' and reg_pattern /= PAT_SENSOR and
                 interval_cnt >= reg_interval and reg_height /= 0 and reg_width /= 0) then
            cfg_words_per_row <= reg_width(15 downto 3);
            cfg_height        <= reg_height;
            cfg_offset_x      <= reg_offset_x;
            cfg_offset_y      <= reg_offset_y;
            cfg_pattern       <= reg_pattern;
            cfg_mono          <= reg_mono;
            cfg_n             <= frame_count(7 downto 0);
            cfg_sensor        <= start_sensor;
            gen_restart       <= '1';
            interval_cnt      <= (others => '0');
            cycle_cnt         <= (others => '0');
            state             <= S_START;
          end if;

        -- ── START: size the frame (one multiply) and point at the buffer ─
        when S_START =>
          words_left <= resize(cfg_words_per_row * cfg_height, 26);
          burst_addr <= buf_addr;
          beats_left <= (others => '0');
          first_word <= '1';
          frame_bad  <= '0';
          state      <= S_BURST;

        -- ── BURST: issue 16-beat bursts until the frame is written ───────
        when S_BURST =>
          if beats_left = 0 then
            -- Address and burstcount hold for all the beats of a burst.
            if words_left = 0 or frame_bad = '1' then
              avm_write <= '0';
              state     <= S_WAIT_RESP;
            elsif reg_enable = '0' and cfg_sensor = '1' then
              frame_bad <= '1';             -- stopped mid-frame: drop it
              avm_write <= '0';
              state     <= S_WAIT_RESP;
            elsif issue then
              avm_burstcount <= std_logic_vector(burst);
              beats_left     <= burst;
              avm_write      <= '1';
              bursts_out     <= bursts_out + 1;
            end if;
          elsif beat = '1' then
            -- Beat accepted: advance to the next pixel word.
            words_left <= words_left - 1;
            beats_left <= beats_left - 1;
            first_word <= '0';
            if cfg_sensor = '1' and head_pixels = '0' then
              frame_bad <= '1';             -- sensor frame ended early
            end if;
            if beats_left = 1 then
              avm_write  <= '0';
              burst_addr <= burst_addr + BURST_WORDS * 8;
            end if;
          end if;
          if avm_writeresponsevalid = '1' then
            if issue then
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
            if cfg_sensor = '0' then
              publish_frame;                  -- test pattern: always complete
              state <= S_IDLE;
            elsif frame_bad = '1' then
              bad_frames <= bad_frames + 1;   -- too short, or stopped mid-frame
              state <= S_IDLE;
            else
              state <= S_CHECK;
            end if;
          end if;

        -- ── CHECK: a sensor frame must end exactly here (trailer next) ───
        when S_CHECK =>
          if fifo_empty = '0' then
            if head_tag = TAG_TRAILER then
              publish_frame;
            else
              bad_frames <= bad_frames + 1;   -- sensor frame was too long
            end if;
            state <= S_IDLE;
          elsif reg_enable = '0' then
            state <= S_IDLE;
          end if;

      end case;
    end if;
  end process;

end architecture rtl;
