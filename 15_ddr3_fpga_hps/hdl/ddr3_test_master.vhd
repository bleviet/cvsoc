library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

--------------------------------------------------------------------------------
-- ddr3_test_master — Avalon-MM master for DDR3 memory test via F2H SDRAM bridge.
--
-- This component has two interfaces:
--   1. Avalon-MM slave (control/status registers) — on the LW H2F bridge,
--      controlled by the HPS ARM CPU.
--   2. Avalon-MM master — connected to the F2H SDRAM bridge for direct
--      DDR3 access from the FPGA fabric.
--
-- Register Map (slave interface, 32-bit aligned):
--   Offset  Name       Access  Description
--   0x00    CTRL       R/W     bit 0: start (write 1 to begin)
--                              bit 1: mode  (0=write, 1=read+verify)
--                              bit 2: running (read-only, 1 while active)
--   0x04    BASE_ADDR  R/W     Start address in DDR3 space
--   0x08    LENGTH     R/W     Number of 32-bit words to transfer
--   0x0C    STATUS     R/O     bit 0: done
--                              bit 1: error (any mismatch detected)
--                              bits [31:16]: error count (saturates at 0xFFFF)
--   0x10    PATTERN    R/W     32-bit test pattern for writes
--                              In write mode: writes PATTERN + word_offset
--                              In read mode:  expects PATTERN + word_offset
--
-- FSM:  IDLE → (start=1) → RUNNING → DONE → (start cleared) → IDLE
--
-- The master performs sequential (non-burst) 32-bit read/write transactions.
-- This is intentionally simple for teaching purposes.
--------------------------------------------------------------------------------
entity ddr3_test_master is
  port (
    clk           : in    std_logic;
    reset_n       : in    std_logic;

    -- ── Avalon-MM Slave interface (control/status registers) ──────────
    avs_address   : in    std_logic_vector(2 downto 0);   -- word address [0..4]
    avs_read      : in    std_logic;
    avs_readdata  : out   std_logic_vector(31 downto 0);
    avs_write     : in    std_logic;
    avs_writedata : in    std_logic_vector(31 downto 0);

    -- ── Avalon-MM Master interface (DDR3 access via F2H SDRAM bridge) ──
    avm_address       : out   std_logic_vector(31 downto 0);
    avm_read          : out   std_logic;
    avm_readdata      : in    std_logic_vector(31 downto 0);
    avm_readdatavalid : in    std_logic;
    avm_write         : out   std_logic;
    avm_writedata     : out   std_logic_vector(31 downto 0);
    avm_waitrequest   : in    std_logic;
    avm_byteenable    : out   std_logic_vector(3 downto 0)
  );
end entity ddr3_test_master;

architecture rtl of ddr3_test_master is

  -- FSM states
  type state_t is (S_IDLE, S_WRITE, S_READ, S_READ_WAIT, S_DONE);
  signal state : state_t := S_IDLE;

  -- Control/status registers
  signal reg_ctrl       : std_logic_vector(31 downto 0) := (others => '0');
  signal reg_base_addr  : std_logic_vector(31 downto 0) := (others => '0');
  signal reg_length     : std_logic_vector(31 downto 0) := (others => '0');
  signal reg_status     : std_logic_vector(31 downto 0) := (others => '0');
  signal reg_pattern    : std_logic_vector(31 downto 0) := (others => '0');

  -- Internal working signals
  signal word_counter   : unsigned(31 downto 0) := (others => '0');
  signal error_count    : unsigned(15 downto 0) := (others => '0');
  signal running        : std_logic := '0';
  signal done           : std_logic := '0';
  signal has_error      : std_logic := '0';

  -- Convenience aliases
  signal start_bit      : std_logic;
  signal mode_bit       : std_logic;  -- 0=write, 1=read+verify
  signal target_length  : unsigned(31 downto 0);
  signal current_addr   : unsigned(31 downto 0);
  signal expected_data  : std_logic_vector(31 downto 0);

begin

  start_bit     <= reg_ctrl(0);
  mode_bit      <= reg_ctrl(1);
  target_length <= unsigned(reg_length);
  current_addr  <= unsigned(reg_base_addr) + (word_counter(29 downto 0) & "00");  -- byte address
  expected_data <= std_logic_vector(unsigned(reg_pattern) + word_counter);

  -- Always enable all 4 bytes
  avm_byteenable <= "1111";

  -- ── Slave read logic ──────────────────────────────────────────────────
  process (avs_address, reg_ctrl, running, reg_base_addr, reg_length,
           reg_status, done, has_error, error_count, reg_pattern)
  begin
    avs_readdata <= (others => '0');
    case avs_address is
      when "000" =>  -- CTRL (0x00)
        avs_readdata <= reg_ctrl;
        avs_readdata(2) <= running;
      when "001" =>  -- BASE_ADDR (0x04)
        avs_readdata <= reg_base_addr;
      when "010" =>  -- LENGTH (0x08)
        avs_readdata <= reg_length;
      when "011" =>  -- STATUS (0x0C)
        avs_readdata(0) <= done;
        avs_readdata(1) <= has_error;
        avs_readdata(31 downto 16) <= std_logic_vector(error_count);
      when "100" =>  -- PATTERN (0x10)
        avs_readdata <= reg_pattern;
      when others =>
        avs_readdata <= (others => '0');
    end case;
  end process;

  -- ── Slave write logic ─────────────────────────────────────────────────
  process (clk, reset_n)
  begin
    if reset_n = '0' then
      reg_ctrl      <= (others => '0');
      reg_base_addr <= (others => '0');
      reg_length    <= (others => '0');
      reg_pattern   <= (others => '0');
    elsif rising_edge(clk) then
      if avs_write = '1' then
        case avs_address is
          when "000" =>  -- CTRL
            reg_ctrl(1 downto 0) <= avs_writedata(1 downto 0);
          when "001" =>  -- BASE_ADDR
            reg_base_addr <= avs_writedata;
          when "010" =>  -- LENGTH
            reg_length <= avs_writedata;
          when "100" =>  -- PATTERN
            reg_pattern <= avs_writedata;
          when others =>
            null;
        end case;
      end if;

      -- Auto-clear start bit once running
      if running = '1' then
        reg_ctrl(0) <= '0';
      end if;
    end if;
  end process;

  -- ── Master FSM ────────────────────────────────────────────────────────
  process (clk, reset_n)
  begin
    if reset_n = '0' then
      state        <= S_IDLE;
      word_counter <= (others => '0');
      error_count  <= (others => '0');
      running      <= '0';
      done         <= '0';
      has_error    <= '0';
      avm_read     <= '0';
      avm_write    <= '0';
      avm_address  <= (others => '0');
      avm_writedata <= (others => '0');

    elsif rising_edge(clk) then
      -- Default: de-assert master signals
      avm_read  <= '0';
      avm_write <= '0';

      case state is

        -- ── IDLE: wait for start ──────────────────────────────────────
        when S_IDLE =>
          if start_bit = '1' then
            word_counter <= (others => '0');
            error_count  <= (others => '0');
            running      <= '1';
            done         <= '0';
            has_error    <= '0';

            if mode_bit = '0' then
              state <= S_WRITE;
            else
              state <= S_READ;
            end if;
          end if;

        -- ── WRITE: hold write signals until slave accepts (waitrequest=0) ─
        -- Staying in S_WRITE until accepted avoids issuing the same
        -- transaction twice (old two-state S_WRITE→S_WRITE_WAIT always
        -- re-asserted write on entry to S_WRITE_WAIT even when waitrequest
        -- was already 0 in S_WRITE, causing a duplicate write).
        when S_WRITE =>
          avm_address   <= std_logic_vector(current_addr);
          avm_writedata <= expected_data;
          avm_write     <= '1';

          if avm_waitrequest = '0' then
            avm_write <= '0';
            if word_counter + 1 >= target_length then
              state <= S_DONE;
            else
              word_counter <= word_counter + 1;
              -- stay in S_WRITE; current_addr and expected_data update next cycle
            end if;
          end if;

        -- ── READ: hold read asserted until the address is accepted ────
        -- Only advance to S_READ_WAIT after waitrequest=0 so exactly one
        -- read request is issued per word.  The old code always moved to
        -- S_READ_WAIT unconditionally and then re-asserted avm_read there,
        -- issuing a duplicate read that produced two readdatavalid pulses.
        when S_READ =>
          avm_address <= std_logic_vector(current_addr);
          avm_read    <= '1';

          if avm_waitrequest = '0' then
            avm_read <= '0';
            state    <= S_READ_WAIT;
          end if;

        -- ── READ_WAIT: wait for the single read response ──────────────
        -- avm_read is already de-asserted; we are just waiting for data.
        when S_READ_WAIT =>
          if avm_readdatavalid = '1' then
            -- Compare read data against expected pattern
            if avm_readdata /= expected_data then
              has_error <= '1';
              if error_count /= x"FFFF" then
                error_count <= error_count + 1;
              end if;
            end if;

            if word_counter + 1 >= target_length then
              state <= S_DONE;
            else
              word_counter <= word_counter + 1;
              state <= S_READ;
            end if;
          end if;

        -- ── DONE: hold until start bit is cleared ─────────────────────
        when S_DONE =>
          running <= '0';
          done    <= '1';
          if start_bit = '0' then
            state <= S_IDLE;
          end if;

      end case;
    end if;
  end process;

end architecture rtl;
