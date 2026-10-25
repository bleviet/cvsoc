"""cocotb tests for hdl/cam_tpg_writer.vhd.

The DUT's Avalon-MM master is connected to a memory model with random
waitrequest back-pressure and delayed write responses. Frames are checked
byte-for-byte against client/tpg_model.py, the same golden model camd's sim
source is tested against.

Run with: python3 sim/run.py
"""

import os
import random
import sys
from pathlib import Path

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import ClockCycles, RisingEdge

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "client"))
import tpg_model  # noqa: E402

SENSOR_WIDTH = int(os.environ.get("SENSOR_WIDTH", "640"))

# CSR word indices
ID, CTRL, STATUS, FRAME_COUNT, WIDTH, HEIGHT, OFFSET_X, OFFSET_Y = range(8)
PATTERN, FORMAT, FRAME_INTERVAL, BUF_BASE, BUF_STRIDE, BUF_COUNT = range(8, 14)
SENSOR_WIDTH_REG, FRAME_CYCLES = 14, 15

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


async def setup(dut, **mem_args):
    cocotb.start_soon(Clock(dut.clk, 20, unit="ns").start())
    dut.reset_n.value = 0
    mem = AvalonMemory(dut, **mem_args)
    csr = Csr(dut)
    await ClockCycles(dut.clk, 3)
    dut.reset_n.value = 1
    await ClockCycles(dut.clk, 2)
    return mem, csr


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
    assert await csr.read(ID) == 0x43564331
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
