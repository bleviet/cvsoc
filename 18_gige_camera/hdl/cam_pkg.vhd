library ieee;
use ieee.std_logic_1164.all;

--------------------------------------------------------------------------------
-- cam_pkg — the word format d5m_capture sends to cam_frame_writer through
-- async_fifo (68 bits per word).
--
--   [67:66] tag
--           TAG_PIXELS  8 pixels, pixel 0 in bits [7:0]
--           TAG_SOF     the same, and the first word of a frame
--           TAG_TRAILER end of frame, sent after FRAME_VALID falls
--           TAG_FILL    15 of these follow every trailer, so every frame
--                       brings at least one full burst of words after its
--                       last pixel: the writer never waits for a frame that
--                       ended early (cam_frame_writer starts a burst only
--                       when all its words are in the FIFO)
--   [65:64] flags (trailer only)
--           bit 64: FIFO overflow, words of this frame were dropped
--           bit 65: lines of this frame had different lengths
--   [63:0]  pixels, or for a trailer:
--           [11:0]  lines in the frame
--           [23:12] pixels in the last line
--           [39:24] line period, PIXCLK cycles from one LINE_VALID rise to the
--                   next (saturates at 0xFFFF)
--           [51:40] OR  of all 12-bit pixels in the frame
--           [63:52] AND of all 12-bit pixels in the frame
--
-- OR and AND expose stuck or swapped data lines during bring-up: with the
-- sensor's color-field test pattern every pixel holds the same value, so
-- OR = AND = that value.
--------------------------------------------------------------------------------
package cam_pkg is

  constant CAP_WORD_WIDTH : positive := 68;

  constant TAG_PIXELS  : std_logic_vector(1 downto 0) := "00";
  constant TAG_SOF     : std_logic_vector(1 downto 0) := "01";
  constant TAG_TRAILER : std_logic_vector(1 downto 0) := "10";
  constant TAG_FILL    : std_logic_vector(1 downto 0) := "11";
  constant FILL_WORDS  : natural := 15;

  constant FLAG_OVERFLOW  : natural := 64;
  constant FLAG_IRREGULAR : natural := 65;

end package cam_pkg;
