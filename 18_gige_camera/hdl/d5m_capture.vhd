library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.cam_pkg.all;

--------------------------------------------------------------------------------
-- d5m_capture — samples the TRDB-D5M (MT9P001) parallel pixel bus.
--
-- Runs entirely in the sensor's PIXCLK domain. camd sets the sensor's
-- Invert_Pixel_Clock bit, so the sensor launches DOUT, FRAME_VALID and
-- LINE_VALID on the falling edge of the PIXCLK pin and this block samples them
-- on the rising edge, half a pixel period later.
--
-- Pixels are reduced to 8 bits (DOUT[11:4]), packed 8 per word, and written to
-- the FIFO. Each frame starts with a TAG_SOF word and ends with a TAG_TRAILER
-- word that carries the measured geometry and statistics, followed by
-- FILL_WORDS fill words (see cam_pkg). A
-- line whose length is not a multiple of 8 ends with a zero-padded word.
--
-- Trim (for the mirrored readout camd uses): the sensor's window starts on an
-- even row and column and has an even size, so a readout mirrored in both
-- directions starts on an odd row and column and the Bayer order becomes
-- G B / R G. With `trim` set, the sensor sends a window one pixel larger on
-- every side, and this block drops the first line of each frame, the first
-- pixel of each line, and everything beyond `max_width` pixels and
-- `max_lines` lines: the first pixel kept is a G1 again (G R / B G).
--
-- `enable` comes from the fabric clock domain and is synchronized here.
-- `trim`, `max_width` and `max_lines` are quasi-static: sampled at each
-- frame start (a frame captured while they change fails the writer's size
-- check).
-- Capture starts at the next FRAME_VALID rise after it goes high, so frames
-- are never cut. When the FIFO is full a word is dropped and the trailer
-- reports it.
--------------------------------------------------------------------------------
entity d5m_capture is
  port (
    pixclk  : in  std_logic;
    d       : in  std_logic_vector(11 downto 0);
    fval    : in  std_logic;
    lval    : in  std_logic;
    enable  : in  std_logic;                     -- asynchronous
    trim      : in  std_logic;                   -- quasi-static, see above
    max_width : in  unsigned(11 downto 0);
    max_lines : in  unsigned(11 downto 0);

    wr_en   : out std_logic;
    wr_data : out std_logic_vector(CAP_WORD_WIDTH - 1 downto 0);
    wr_full : in  std_logic
  );
end entity d5m_capture;

architecture rtl of d5m_capture is

  -- Input registers: placed in the I/O cells (FAST_INPUT_REGISTER).
  signal d_r     : std_logic_vector(11 downto 0) := (others => '0');
  signal fval_r  : std_logic := '0';
  signal lval_r  : std_logic := '0';
  signal fval_d  : std_logic := '0';
  signal lval_d  : std_logic := '0';

  signal en_s1, en_s2 : std_logic := '0';

  signal active      : std_logic := '0';
  signal sof_pending : std_logic := '0';
  signal pack        : std_logic_vector(63 downto 0) := (others => '0');
  signal k           : unsigned(2 downto 0)  := (others => '0');
  signal pix_cnt     : unsigned(11 downto 0) := (others => '0');
  signal last_px     : unsigned(11 downto 0) := (others => '0');
  signal lines       : unsigned(11 downto 0) := (others => '0');
  signal lp_cnt      : unsigned(15 downto 0) := (others => '0');
  signal line_period : unsigned(15 downto 0) := (others => '0');
  signal or_acc      : std_logic_vector(11 downto 0) := (others => '0');
  signal and_acc     : std_logic_vector(11 downto 0) := (others => '1');
  signal overflow    : std_logic := '0';
  signal irregular   : std_logic := '0';
  signal col         : unsigned(11 downto 0) := (others => '0');   -- pixels seen in the line
  signal raw_lines   : unsigned(11 downto 0) := (others => '0');   -- lines seen in the frame
  signal trim_f      : std_logic := '0';
  signal width_f     : unsigned(11 downto 0) := (others => '0');
  signal lines_f     : unsigned(11 downto 0) := (others => '0');

  signal trailer_pending : std_logic := '0';
  signal fill_left       : unsigned(4 downto 0) := (others => '0');
  signal trailer_word    : std_logic_vector(CAP_WORD_WIDTH - 1 downto 0) := (others => '0');

  attribute altera_attribute : string;
  attribute altera_attribute of en_s1 : signal is
    "-name SYNCHRONIZER_IDENTIFICATION FORCED_IF_ASYNCHRONOUS";

begin

  process (pixclk)
    variable v_active    : std_logic;
    variable v_sof       : std_logic;
    variable v_pack      : std_logic_vector(63 downto 0);
    variable v_k         : unsigned(2 downto 0);
    variable v_pix       : unsigned(11 downto 0);
    variable v_last_px   : unsigned(11 downto 0);
    variable v_lines     : unsigned(11 downto 0);
    variable v_lp        : unsigned(15 downto 0);
    variable v_period    : unsigned(15 downto 0);
    variable v_or        : std_logic_vector(11 downto 0);
    variable v_and       : std_logic_vector(11 downto 0);
    variable v_overflow  : std_logic;
    variable v_irregular : std_logic;
    variable v_col       : unsigned(11 downto 0);
    variable v_raw       : unsigned(11 downto 0);
    variable keep_line   : boolean;
    variable v_pending   : std_logic;
    variable v_fill      : unsigned(4 downto 0);
    variable v_trailer   : std_logic_vector(CAP_WORD_WIDTH - 1 downto 0);
    variable emit        : boolean;
    variable emit_word   : std_logic_vector(CAP_WORD_WIDTH - 1 downto 0);

    -- Queue the packed word (at most one per cycle).
    procedure push_pack is
    begin
      emit := true;
      if v_sof = '1' then
        emit_word := TAG_SOF & "00" & v_pack;
      else
        emit_word := TAG_PIXELS & "00" & v_pack;
      end if;
      v_sof  := '0';
      v_k    := (others => '0');
      v_pack := (others => '0');
    end procedure;

  begin
    if rising_edge(pixclk) then
      d_r    <= d;
      fval_r <= fval;
      lval_r <= lval;
      fval_d <= fval_r;
      lval_d <= lval_r;
      en_s1  <= enable;
      en_s2  <= en_s1;

      v_active    := active;
      v_sof       := sof_pending;
      v_pack      := pack;
      v_k         := k;
      v_pix       := pix_cnt;
      v_last_px   := last_px;
      v_lines     := lines;
      v_period    := line_period;
      v_or        := or_acc;
      v_and       := and_acc;
      v_overflow  := overflow;
      v_irregular := irregular;
      v_col       := col;
      v_raw       := raw_lines;
      v_pending   := trailer_pending;
      v_fill      := fill_left;
      v_trailer   := trailer_word;
      emit        := false;
      emit_word   := (others => '0');

      if lp_cnt /= x"FFFF" then
        v_lp := lp_cnt + 1;
      else
        v_lp := lp_cnt;
      end if;

      -- ── Frame start ─────────────────────────────────────────────────
      if fval_r = '1' and fval_d = '0' then
        v_active    := en_s2;
        v_sof       := '1';
        v_pack      := (others => '0');
        v_k         := (others => '0');
        v_pix       := (others => '0');
        v_lines     := (others => '0');
        v_or        := (others => '0');
        v_and       := (others => '1');
        v_overflow  := '0';
        v_irregular := '0';
        v_col       := (others => '0');
        v_raw       := (others => '0');
        trim_f      <= trim;
        width_f     <= max_width;
        lines_f     <= max_lines;
      end if;

      -- ── Line period (measured between consecutive LINE_VALID rises) ─
      if lval_r = '1' and lval_d = '0' then
        if v_active = '1' and v_lines /= 0 then
          v_period := v_lp;
        end if;
        v_lp := (others => '0');
      end if;

      if v_active = '1' then
        -- Trim (sampled at the frame start; trim_f is valid from the
        -- first line on, which comes many cycles after FRAME_VALID).
        keep_line := trim_f = '0' or (v_raw /= 0 and v_lines < lines_f);

        -- ── Pixel ─────────────────────────────────────────────────────
        if lval_r = '1' then
          if keep_line and (trim_f = '0' or (v_col /= 0 and v_pix < width_f)) then
            v_pack(8 * to_integer(v_k) + 7 downto 8 * to_integer(v_k)) := d_r(11 downto 4);
            v_or  := v_or or d_r;
            v_and := v_and and d_r;
            v_pix := v_pix + 1;
            if v_k = 7 then
              push_pack;
            else
              v_k := v_k + 1;
            end if;
          end if;
          v_col := v_col + 1;
        end if;

        -- ── Line end ──────────────────────────────────────────────────
        if lval_r = '0' and lval_d = '1' then
          if keep_line then
            if v_k /= 0 then
              push_pack;               -- zero-padded partial word
            end if;
            if v_lines /= 0 and v_pix /= v_last_px then
              v_irregular := '1';
            end if;
            v_last_px := v_pix;
            v_lines   := v_lines + 1;
          end if;
          v_raw := v_raw + 1;
          v_col := (others => '0');
          v_pix := (others => '0');
        end if;
      end if;

      -- ── FIFO write: pixels first, the trailer in a free cycle ───────
      wr_en <= '0';
      if emit then
        if wr_full = '1' then
          v_overflow := '1';
        else
          wr_en   <= '1';
          wr_data <= emit_word;
        end if;
      end if;

      -- ── Frame end: queue the trailer (after this cycle's overflow) ──
      if fval_r = '0' and fval_d = '1' and v_active = '1' then
        v_active  := '0';
        v_pending := '1';
        v_trailer := TAG_TRAILER & v_irregular & v_overflow & v_and & v_or &
                     std_logic_vector(v_period) & std_logic_vector(v_last_px) &
                     std_logic_vector(v_lines);
      end if;

      if not emit and wr_full = '0' then
        if v_pending = '1' then
          wr_en     <= '1';
          wr_data   <= v_trailer;
          v_pending := '0';
          v_fill    := to_unsigned(FILL_WORDS, 5);
        elsif v_fill /= 0 then
          wr_en   <= '1';
          wr_data <= TAG_FILL & (CAP_WORD_WIDTH - 3 downto 0 => '0');
          v_fill  := v_fill - 1;
        end if;
      end if;

      active          <= v_active;
      sof_pending     <= v_sof;
      pack            <= v_pack;
      k               <= v_k;
      pix_cnt         <= v_pix;
      last_px         <= v_last_px;
      lines           <= v_lines;
      lp_cnt          <= v_lp;
      line_period     <= v_period;
      or_acc          <= v_or;
      and_acc         <= v_and;
      overflow        <= v_overflow;
      irregular       <= v_irregular;
      col             <= v_col;
      raw_lines       <= v_raw;
      trailer_pending <= v_pending;
      fill_left       <= v_fill;
      trailer_word    <= v_trailer;
    end if;
  end process;

end architecture rtl;
