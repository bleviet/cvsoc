"""cocotb tests for hdl/cam_frame_writer.vhd (with d5m_capture and async_fifo).

The DUT's Avalon-MM master is connected to a memory model with random
waitrequest back-pressure and delayed write responses. Test-pattern frames are
checked byte-for-byte against client/tpg_model.py, the same golden model
camd's sim source is tested against. Sensor frames come from a model of the
D5M's parallel bus in its own PIXCLK domain.

Run with: python3 sim/run.py
"""

import os
import random
import sys
from pathlib import Path

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import ClockCycles, FallingEdge, RisingEdge, Timer

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "client"))
import tpg_model  # noqa: E402

SENSOR_WIDTH = int(os.environ.get("SENSOR_WIDTH", "640"))

# CSR word indices
ID, CTRL, STATUS, FRAME_COUNT, WIDTH, HEIGHT, OFFSET_X, OFFSET_Y = range(8)
PATTERN, FORMAT, FRAME_INTERVAL, BUF_BASE, BUF_STRIDE, BUF_COUNT = range(8, 14)
SENSOR_WIDTH_REG, FRAME_CYCLES = 14, 15
SENSOR_CTRL, I2C, CAP_FRAMES, CAP_SIZE, CAP_LINE_PERIOD, CAP_BITS = range(16, 22)
CAP_FLAGS, CAP_OVERFLOWS, FRAME_PERIOD, BAD_FRAMES = range(22, 26)

SENSOR_RESET_N, SENSOR_XCLK, SENSOR_TRIGGER, SENSOR_CAPTURE = 1, 2, 4, 8
PATTERN_SENSOR = 0

FMT_BAYER, FMT_MONO = 0, 1


class AvalonMemory:
    """Avalon-MM burst write slave with back-pressure and write responses."""

    def __init__(self, dut, stall=0.3, resp_delay=(1, 12), seed=1):
        self.dut = dut
        self.mem = {}
        self.bursts = []          # (address, burstcount)
        self.stall = stall
        self.resp_delay = resp_delay
        self.rng = random.Random(seed)
        self.hold_responses = False
        self.error_next = False
        dut.avm_waitrequest.value = 1
        dut.avm_writeresponsevalid.value = 0
        dut.avm_response.value = 0
        cocotb.start_soon(self.run())

    async def run(self):
        dut = self.dut
        wait = 1
        beat = 0
        addr = count = 0
        pending = []              # cycles until each queued response
        while True:
            await RisingEdge(dut.clk)
            # Sample what the DUT presented at this edge.
            if str(dut.avm_write.value) == "1" and not wait:
                if beat == 0:
                    addr = int(dut.avm_address.value)
                    count = int(dut.avm_burstcount.value)
                    self.bursts.append((addr, count))
                data = int(dut.avm_writedata.value)
                for k in range(8):
                    self.mem[addr + beat * 8 + k] = (data >> (8 * k)) & 0xFF
                beat += 1
                if beat == count:
                    beat = 0
                    pending.append([self.rng.randint(*self.resp_delay), self.error_next])
                    self.error_next = False
            # Drive the next cycle's waitrequest and response.
            wait = 1 if self.rng.random() < self.stall else 0
            dut.avm_waitrequest.value = wait
            dut.avm_writeresponsevalid.value = 0
            if pending and not self.hold_responses:
                for p in pending:
                    p[0] -= 1
                if pending[0][0] <= 0:
                    _, err = pending.pop(0)
                    dut.avm_writeresponsevalid.value = 1
                    dut.avm_response.value = 2 if err else 0

    def read(self, addr, n):
        return bytes(self.mem.get(addr + i, 0xEE) for i in range(n))


class Csr:
    def __init__(self, dut):
        self.dut = dut
        dut.avs_address.value = 0
        dut.avs_read.value = 0
        dut.avs_write.value = 0
        dut.avs_writedata.value = 0

    async def write(self, reg, value):
        dut = self.dut
        await RisingEdge(dut.clk)
        dut.avs_address.value = reg
        dut.avs_writedata.value = value
        dut.avs_write.value = 1
        await RisingEdge(dut.clk)
        dut.avs_write.value = 0

    async def read(self, reg):
        dut = self.dut
        await RisingEdge(dut.clk)
        dut.avs_address.value = reg
        dut.avs_read.value = 1
        await RisingEdge(dut.clk)          # readdata is combinational
        value = int(dut.avs_readdata.value)
        dut.avs_read.value = 0
        return value


class D5M:
    """The TRDB-D5M parallel bus: PIXCLK, FRAME_VALID, LINE_VALID, DOUT[11:0].

    Outputs change on the falling edge of PIXCLK (the sensor's
    Invert_Pixel_Clock mode), half a period before the DUT samples them.
    `pixel(x, y, n)` gives the 12-bit value of pixel (x, y) in frame n.
    """

    def __init__(self, dut, period_ns=40, hblank=24, vblank=60):
        self.dut = dut
        self.hblank = hblank
        self.vblank = vblank
        self.frames_sent = 0
        dut.d5m_d.value = 0
        dut.d5m_fval.value = 0
        dut.d5m_lval.value = 0
        cocotb.start_soon(Clock(dut.d5m_pixclk, period_ns, unit="ns").start())

    @staticmethod
    def pixel(x, y, n):
        return (x * 37 + y * 101 + n * 613) & 0xFFF

    async def frame(self, width, height, pixel=None):
        dut = self.dut
        pixel = pixel or self.pixel
        n = self.frames_sent
        await FallingEdge(dut.d5m_pixclk)
        dut.d5m_fval.value = 1
        for y in range(height):
            for _ in range(self.hblank):
                await FallingEdge(dut.d5m_pixclk)
            dut.d5m_lval.value = 1
            for x in range(width):
                dut.d5m_d.value = pixel(x, y, n)
                await FallingEdge(dut.d5m_pixclk)
            dut.d5m_lval.value = 0
            dut.d5m_d.value = 0
        for _ in range(self.hblank):
            await FallingEdge(dut.d5m_pixclk)
        dut.d5m_fval.value = 0
        for _ in range(self.vblank):
            await FallingEdge(dut.d5m_pixclk)
        self.frames_sent += 1

    def expected(self, width, height, n, pixel=None):
        pixel = pixel or self.pixel
        return bytes(pixel(x, y, n) >> 4 for y in range(height) for x in range(width))


async def setup(dut, **mem_args):
    """Start the fabric clock and the memory model, then reset. Once per test."""
    cocotb.start_soon(Clock(dut.clk, 20, unit="ns").start())
    dut.d5m_strobe.value = 0
    dut.d5m_scl_in.value = 1
    dut.d5m_sda_in.value = 1
    mem = AvalonMemory(dut, **mem_args)
    csr = Csr(dut)
    await reset(dut)
    return mem, csr


async def reset(dut):
    dut.reset_n.value = 0
    await ClockCycles(dut.clk, 3)
    dut.reset_n.value = 1
    await ClockCycles(dut.clk, 2)


async def configure(csr, width, height, ox=0, oy=0, pattern=tpg_model.GREY_HRAMP_MOVING,
                    fmt=FMT_BAYER, base=0x3C000000, stride=0x10000, count=1, interval=0):
    for reg, value in ((WIDTH, width), (HEIGHT, height), (OFFSET_X, ox), (OFFSET_Y, oy),
                       (PATTERN, pattern), (FORMAT, fmt), (BUF_BASE, base),
                       (BUF_STRIDE, stride), (BUF_COUNT, count), (FRAME_INTERVAL, interval)):
        await csr.write(reg, value)


async def wait_frames(csr, n, timeout_cycles=200000):
    for _ in range(timeout_cycles // 8):
        if await csr.read(FRAME_COUNT) >= n:
            return
        await ClockCycles(csr.dut.clk, 6)
    raise AssertionError(f"timeout waiting for {n} frames")


def expected(pattern, fmt, w, h, ox, oy, n):
    pfnc = tpg_model.MONO8 if fmt == FMT_MONO else tpg_model.BAYER_GR8
    return tpg_model.frame(pattern, pfnc, w, h, ox, oy, n, SENSOR_WIDTH)


@cocotb.test()
async def test_id_and_sensor_width(dut):
    _, csr = await setup(dut)
    assert await csr.read(ID) == 0x43564332
    assert await csr.read(SENSOR_WIDTH_REG) == SENSOR_WIDTH


@cocotb.test()
async def test_ramp_frame_matches_model(dut):
    mem, csr = await setup(dut)
    await configure(csr, 64, 6)
    await csr.write(CTRL, 1)
    await wait_frames(csr, 1)
    await csr.write(CTRL, 0)
    assert mem.read(0x3C000000, 64 * 6) == expected(tpg_model.GREY_HRAMP_MOVING, FMT_BAYER,
                                                   64, 6, 0, 0, 0)


@cocotb.test()
async def test_color_bar_roi_bayer_and_mono(dut):
    for fmt in (FMT_BAYER, FMT_MONO):
        mem, csr = await setup(dut, seed=fmt + 7)
        # Odd OffsetY exercises the CFA phase; OffsetX (a multiple of 8, like
        # the hardware register) sits just before the first bar edge.
        w, h, oy = SENSOR_WIDTH // 2, 5, 3
        ox = (SENSOR_WIDTH // 8) // 8 * 8 - 8
        await configure(csr, w, h, ox, oy, tpg_model.COLOR_BAR, fmt)
        await csr.write(CTRL, 1)
        await wait_frames(csr, 1)
        await csr.write(CTRL, 0)
        assert mem.read(0x3C000000, w * h) == expected(tpg_model.COLOR_BAR, fmt, w, h, ox, oy, 0)


@cocotb.test()
async def test_ring_buffers_and_moving_ramp(dut):
    mem, csr = await setup(dut)
    base, stride = 0x3C000000, 0x1000
    await configure(csr, 128, 4, base=base, stride=stride, count=3)
    await csr.write(CTRL, 1)
    await wait_frames(csr, 5)
    await csr.write(CTRL, 0)
    await ClockCycles(dut.clk, 2000)
    frames = await csr.read(FRAME_COUNT)
    last = (await csr.read(STATUS) >> 8) & 0xF
    assert last == (frames - 1) % 3
    # Each buffer holds the most recent frame written to it.
    for buf in range(3):
        n = max(k for k in range(frames) if k % 3 == buf)
        got = mem.read(base + buf * stride, 128 * 4)
        assert got == expected(tpg_model.GREY_HRAMP_MOVING, FMT_BAYER, 128, 4, 0, 0, n), buf


@cocotb.test()
async def test_bursts_are_aligned_and_bounded(dut):
    mem, csr = await setup(dut)
    w, h = 200, 7                     # 175 words: ends in a short burst
    await configure(csr, w, h)
    await csr.write(CTRL, 1)
    await wait_frames(csr, 1)
    await csr.write(CTRL, 0)
    first = [b for b in mem.bursts if b[0] < 0x3C000000 + 0x10000]
    total = 0
    for addr, count in first[: -(-w * h // 128)]:
        assert 1 <= count <= 16
        assert addr % 128 == 0
        assert addr // 4096 == (addr + count * 8 - 1) // 4096, "crosses 4 KB"
        total += count * 8
    assert total == w * h


@cocotb.test()
async def test_frame_not_done_until_responses(dut):
    mem, csr = await setup(dut, stall=0.0)
    mem.hold_responses = True
    await configure(csr, 64, 4)
    await csr.write(CTRL, 1)
    await ClockCycles(dut.clk, 300)
    assert await csr.read(FRAME_COUNT) == 0
    assert await csr.read(STATUS) & 1, "busy while responses are outstanding"
    mem.hold_responses = False
    await wait_frames(csr, 1)
    await csr.write(CTRL, 0)


@cocotb.test()
async def test_error_response_sets_status(dut):
    mem, csr = await setup(dut)
    mem.error_next = True
    await configure(csr, 64, 2)
    await csr.write(CTRL, 1)
    await wait_frames(csr, 1)
    await csr.write(CTRL, 0)
    assert await csr.read(STATUS) & 2
    await ClockCycles(dut.clk, 200)
    await csr.write(CTRL, 1)          # re-enable clears the error
    await ClockCycles(dut.clk, 5)
    assert not (await csr.read(STATUS) & 2)
    await csr.write(CTRL, 0)


@cocotb.test()
async def test_frame_interval(dut):
    mem, csr = await setup(dut, stall=0.0, resp_delay=(1, 1))
    interval = 3000
    await configure(csr, 64, 2, interval=interval)
    await csr.write(CTRL, 1)
    starts = []
    seen = 0
    for _ in range(20000):
        await RisingEdge(dut.clk)
        if len(mem.bursts) > seen:
            starts.append(cocotb.utils.get_sim_time("ns"))
            seen = len(mem.bursts)
        if len(starts) == 4:
            break
    await csr.write(CTRL, 0)
    gaps = [(b - a) / 20 for a, b in zip(starts, starts[1:])]
    for g in gaps:
        assert abs(g - interval) <= 4, gaps


# ── Sensor path ───────────────────────────────────────────────────────────────

async def sensor_setup(dut, period_ns=40, width=64, height=6, count=3, **mem_args):
    mem, csr = await setup(dut, **mem_args)
    d5m = D5M(dut, period_ns)
    await sensor_config(csr, width, height, count)
    return mem, csr, d5m


async def sensor_config(csr, width, height, count=3):
    await configure(csr, width, height, pattern=PATTERN_SENSOR, count=count)
    await csr.write(SENSOR_CTRL, SENSOR_RESET_N | SENSOR_XCLK | SENSOR_CAPTURE)
    await ClockCycles(csr.dut.clk, 10)   # capture enable crosses into PIXCLK


@cocotb.test()
async def test_sensor_frames_match_bus(dut):
    """Frames from the bus land in the ring byte-exact, as DOUT[11:4], with
    PIXCLK slower (25 MHz) and faster (96 MHz) than the fabric clock."""
    await run_sensor_frames(dut, 40)


@cocotb.test()
async def test_sensor_frames_match_bus_fast_pixclk(dut):
    await run_sensor_frames(dut, 10.4)


async def run_sensor_frames(dut, period):
    mem, csr, d5m = await sensor_setup(dut, period_ns=period, seed=int(period))
    await csr.write(CTRL, 1)
    for _ in range(4):
        await d5m.frame(64, 6)
    await wait_frames(csr, 4)
    await csr.write(CTRL, 0)
    for n in range(1, 4):                           # buffer n % 3 holds frame n
        got = mem.read(0x3C000000 + (n % 3) * 0x10000, 64 * 6)
        assert got == d5m.expected(64, 6, n), (period, n)
    assert await csr.read(BAD_FRAMES) == 0
    assert await csr.read(CAP_OVERFLOWS) == 0


@cocotb.test()
async def test_sensor_trailer_statistics(dut):
    mem, csr, d5m = await sensor_setup(dut)
    await csr.write(CTRL, 1)
    await d5m.frame(64, 6, pixel=lambda x, y, n: 0x5A5 if x % 2 else 0x5A1)
    await d5m.frame(64, 6)
    await ClockCycles(dut.clk, 200)
    assert await csr.read(CAP_FRAMES) == 2
    size = await csr.read(CAP_SIZE)
    assert (size & 0xFFF, size >> 16) == (6, 64)
    assert await csr.read(CAP_LINE_PERIOD) == 64 + d5m.hblank
    assert await csr.read(CAP_FLAGS) == 0
    # OR/AND of one frame show which data bits toggle.
    await d5m.frame(64, 6, pixel=lambda x, y, n: 0x5A5 if x % 2 else 0x5A1)
    await ClockCycles(dut.clk, 200)
    bits = await csr.read(CAP_BITS)
    assert (bits & 0xFFF, bits >> 16) == (0x5A5, 0x5A1)
    # Back-to-back frames: trailers one frame period apart, in fabric clocks.
    await d5m.frame(64, 6)
    await d5m.frame(64, 6)
    await ClockCycles(dut.clk, 200)
    frame_ns = ((64 + d5m.hblank) * 6 + d5m.hblank + d5m.vblank + 1) * 40
    assert abs(await csr.read(FRAME_PERIOD) * 20 - frame_ns) <= 200
    await csr.write(CTRL, 0)


@cocotb.test()
async def test_sensor_wrong_size_frames_are_dropped(dut):
    mem, csr, d5m = await sensor_setup(dut)
    await csr.write(CTRL, 1)
    await d5m.frame(64, 5)              # too short
    await d5m.frame(64, 7)              # too long
    await d5m.frame(56, 6)              # narrower
    await ClockCycles(dut.clk, 300)
    assert await csr.read(FRAME_COUNT) == 0
    assert await csr.read(BAD_FRAMES) == 3
    await d5m.frame(64, 6)
    await wait_frames(csr, 1)
    await csr.write(CTRL, 0)
    assert mem.read(0x3C000000, 64 * 6) == d5m.expected(64, 6, 3)


@cocotb.test()
async def test_sensor_overflow_is_reported_and_recovers(dut):
    mem, csr, d5m = await sensor_setup(dut, width=256, height=24)
    mem.stall = 1.0                     # DDR never accepts: the FIFO fills
    await csr.write(CTRL, 1)
    await d5m.frame(256, 24)            # 768 words > 512-word FIFO
    # The trailer reports the overflow once the writer drains the FIFO again.
    mem.stall = 0.2
    for _ in range(3):
        await d5m.frame(256, 24)
    await ClockCycles(dut.clk, 500)
    assert await csr.read(CAP_FRAMES) == 4
    assert await csr.read(CAP_OVERFLOWS) == 1
    assert await csr.read(BAD_FRAMES) == 1
    assert await csr.read(FRAME_COUNT) == 3
    await csr.write(CTRL, 0)
    last = (await csr.read(STATUS) >> 8) & 0xF
    assert mem.read(0x3C000000 + last * 0x10000, 256 * 24) == d5m.expected(256, 24, 3)


@cocotb.test()
async def test_test_pattern_drains_running_sensor(dut):
    """The sensor keeps streaming while a test pattern is selected: its words
    are discarded, so neither path overflows or corrupts the other."""
    mem, csr, d5m = await sensor_setup(dut, width=128, height=4)
    await csr.write(PATTERN, tpg_model.GREY_HRAMP_MOVING)
    await csr.write(CTRL, 1)
    for _ in range(3):
        await d5m.frame(128, 4)
    await csr.write(CTRL, 0)
    await ClockCycles(dut.clk, 200)
    assert await csr.read(CAP_FRAMES) == 3
    assert await csr.read(CAP_OVERFLOWS) == 0
    frames = await csr.read(FRAME_COUNT)
    last = (await csr.read(STATUS) >> 8) & 0xF
    assert mem.read(0x3C000000 + last * 0x10000, 128 * 4) == expected(
        tpg_model.GREY_HRAMP_MOVING, FMT_BAYER, 128, 4, 0, 0, frames - 1)


@cocotb.test()
async def test_capture_waits_for_frame_start(dut):
    """Enabling capture in the middle of a frame skips that frame."""
    mem, csr = await setup(dut)
    d5m = D5M(dut)
    await configure(csr, 64, 6, pattern=PATTERN_SENSOR)
    await csr.write(CTRL, 1)
    first = cocotb.start_soon(d5m.frame(64, 6))
    await ClockCycles(dut.clk, 400)      # mid-frame
    await csr.write(SENSOR_CTRL, SENSOR_CAPTURE)
    await first
    await ClockCycles(dut.clk, 100)
    assert await csr.read(CAP_FRAMES) == 0
    await d5m.frame(64, 6)
    await wait_frames(csr, 1)
    assert await csr.read(BAD_FRAMES) == 0
    assert mem.read(0x3C000000, 64 * 6) == d5m.expected(64, 6, 1)


@cocotb.test()
async def test_sensor_pins_and_i2c(dut):
    mem, csr = await setup(dut)
    assert int(dut.d5m_reset_n.value) == 0, "sensor held in reset after power-up"
    await csr.write(SENSOR_CTRL, SENSOR_RESET_N | SENSOR_TRIGGER)
    await ClockCycles(dut.clk, 2)
    assert (int(dut.d5m_reset_n.value), int(dut.d5m_trigger.value)) == (1, 1)
    # XCLKIN: half the fabric clock while enabled.
    await csr.write(SENSOR_CTRL, SENSOR_XCLK)
    await ClockCycles(dut.clk, 2)
    seen = []
    for _ in range(8):
        await RisingEdge(dut.clk)
        await Timer(1, unit="ns")
        seen.append(int(dut.d5m_xclkin.value))
    assert seen in ([0, 1] * 4, [1, 0] * 4), seen
    # I2C: open-drain enables out, pin levels in.
    await csr.write(I2C, 0b01)
    await ClockCycles(dut.clk, 2)
    assert (int(dut.d5m_scl_oe.value), int(dut.d5m_sda_oe.value)) == (1, 0)
    dut.d5m_sda_in.value = 0
    dut.d5m_strobe.value = 1
    await ClockCycles(dut.clk, 3)
    value = await csr.read(I2C)
    assert value & 0x3 == 0b01
    assert (value >> 8) & 0x7 == 0b101, hex(value)   # STROBE=1, SDATA=0, SCLK=1
