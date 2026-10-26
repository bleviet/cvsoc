"""cocotb tests for hdl/hdmi_preview.vhd on a tiny screen.

run.py builds the DUT with a 64 x 8 active area so a display frame takes a
few thousand pixel clocks. A frame of BayerGR8 bytes sits in an Avalon-MM
read-memory model with random waitrequest and read latency; every pixel of
a displayed frame is checked against model() below.

Run with: python3 sim/run.py
"""

import random

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import ClockCycles, FallingEdge, RisingEdge

# Must match the generics in run.py.
H_ACTIVE, H_FP, H_SYNC, H_BP = 64, 4, 4, 8
V_ACTIVE, V_FP, V_SYNC, V_BP = 8, 1, 1, 2

ID, CTRL, STATUS, SRC_X, SRC_Y, FRAMES, UNDERFLOWS, I2C, LUT = range(9)
SHOW, GREY, BARS, MAP = 1, 2, 4, 8

BASE = 0x3C000000


class ReadMemory:
    """Avalon-MM burst read slave: random waitrequest, in-order responses."""

    def __init__(self, dut, stall=0.3, latency=(2, 12), seed=3):
        self.dut = dut
        self.mem = {}
        self.stall = stall
        self.latency = latency
        self.rng = random.Random(seed)
        self.bursts = []
        dut.avm_waitrequest.value = 1
        dut.avm_readdatavalid.value = 0
        dut.avm_readdata.value = 0
        cocotb.start_soon(self.run())

    def load(self, addr, data):
        for i, b in enumerate(data):
            self.mem[addr + i] = b

    async def run(self):
        dut = self.dut
        wait = 1
        queue = []                        # [cycles until first beat, addr, count]
        while True:
            await RisingEdge(dut.clk)
            if str(dut.avm_read.value) == "1" and not wait:
                addr = int(dut.avm_address.value)
                count = int(dut.avm_burstcount.value)
                self.bursts.append((addr, count))
                queue.append([self.rng.randint(*self.latency), addr, count])
            wait = 1 if self.rng.random() < self.stall else 0
            dut.avm_waitrequest.value = wait
            dut.avm_readdatavalid.value = 0
            if queue:
                head = queue[0]
                if head[0] > 0:
                    head[0] -= 1
                else:
                    word = sum(self.mem.get(head[1] + k, 0) << (8 * k) for k in range(8))
                    dut.avm_readdata.value = word
                    dut.avm_readdatavalid.value = 1
                    head[1] += 8
                    head[2] -= 1
                    if head[2] == 0:
                        queue.pop(0)


class Csr:
    def __init__(self, dut):
        self.dut = dut
        dut.avs_address.value = 0
        dut.avs_read.value = 0
        dut.avs_write.value = 0
        dut.avs_writedata.value = 0

    async def write(self, reg, value):
        await RisingEdge(self.dut.clk)
        self.dut.avs_address.value = reg
        self.dut.avs_writedata.value = value & 0xFFFFFFFF
        self.dut.avs_write.value = 1
        await RisingEdge(self.dut.clk)
        self.dut.avs_write.value = 0

    async def read(self, reg):
        await RisingEdge(self.dut.clk)
        self.dut.avs_address.value = reg
        self.dut.avs_read.value = 1
        await RisingEdge(self.dut.clk)
        value = int(self.dut.avs_readdata.value)
        self.dut.avs_read.value = 0
        return value


def frame_bytes(w, h):
    return bytes((x * 7 + y * 31 + (x * y) % 13) & 0xFF for y in range(h) for x in range(w))


def model(data, w, h, src_x, src_y, mode, lut=None):
    """Expected (R, G, B) of every active pixel, row by row."""
    def px(x, y):
        return data[y * w + x] if 0 <= x < w and 0 <= y < h else 0

    out = []
    for r in range(V_ACTIVE):
        ya = src_y + (r & ~1)
        row = []
        for c in range(H_ACTIVE):
            if mode & BARS:
                bar = c * 8 // H_ACTIVE
                row.append((255 if bar in (0, 1, 4, 5) else 0, 255 if bar <= 3 else 0,
                            255 if bar % 2 == 0 else 0))
                continue
            if not mode & SHOW:
                row.append((0, 0, 0))
                continue
            x = src_x + c
            if mode & GREY:
                v = px(x, ya + (r & 1))
                rgb = (v, v, v)
            else:
                xa = x & ~1
                g1, rr = px(xa, ya), px(xa + 1, ya)
                b, g2 = px(xa, ya + 1), px(xa + 1, ya + 1)
                rgb = (rr, (g1 + g2) // 2, b)
            if mode & MAP and lut:
                rgb = tuple(lut[v] for v in rgb)
            row.append(rgb)
        out.append(row)
    return out


async def capture_frame(dut):
    """Pixels of the next complete display frame (starting at a VS rise)."""
    while True:
        await RisingEdge(dut.pclk)
        if str(dut.hdmi_vs.value) == "1":
            break
    while str(dut.hdmi_vs.value) == "1":
        await RisingEdge(dut.pclk)
    rows, row = [], []
    while len(rows) < V_ACTIVE:
        await FallingEdge(dut.pclk)
        if str(dut.hdmi_de.value) == "1":
            v = int(dut.hdmi_d.value)
            row.append((v >> 16, (v >> 8) & 0xFF, v & 0xFF))
            if len(row) == H_ACTIVE:
                rows.append(row)
                row = []
    return rows


async def setup(dut, w, h, **mem_args):
    cocotb.start_soon(Clock(dut.clk, 20, unit="ns").start())
    cocotb.start_soon(Clock(dut.pclk, 13.468, unit="ns").start())     # 74.25 MHz
    dut.reset_n.value = 0
    dut.hdmi_int.value = 0
    dut.hdmi_scl_in.value = 1
    dut.hdmi_sda_in.value = 1
    mem = ReadMemory(dut, **mem_args)
    data = frame_bytes(w, h)
    mem.load(BASE, data)
    dut.frame_addr.value = BASE
    dut.frame_width.value = w
    dut.frame_height.value = h
    dut.frame_valid.value = 1
    csr = Csr(dut)
    await ClockCycles(dut.clk, 3)
    dut.reset_n.value = 1
    await ClockCycles(dut.clk, 3)
    return mem, csr, data


async def check(dut, csr, data, w, h, src_x, src_y, mode, lut=None):
    await csr.write(SRC_X, src_x)
    await csr.write(SRC_Y, src_y)
    await csr.write(CTRL, mode)
    await capture_frame(dut)                 # settles the new settings
    got = await capture_frame(dut)
    want = model(data, w, h, src_x, src_y, mode, lut)
    for r in range(V_ACTIVE):
        assert got[r] == want[r], f"row {r} mode {mode} src ({src_x},{src_y})"


@cocotb.test()
async def test_registers(dut):
    _, csr, _ = await setup(dut, 64, 8)
    assert await csr.read(ID) == 0x48444D31
    assert await csr.read(STATUS) & 1
    await csr.write(SRC_X, -40)
    assert await csr.read(SRC_X) == (-40) & 0xFFFFFFFF
    await csr.write(SRC_X, 13)                # rounded down to a multiple of 8
    assert await csr.read(SRC_X) == 8


@cocotb.test()
async def test_color_window_inside_frame(dut):
    w, h = 128, 20
    _, csr, data = await setup(dut, w, h)
    await check(dut, csr, data, w, h, 32, 6, SHOW)
    assert await csr.read(UNDERFLOWS) == 0
    assert await csr.read(FRAMES) >= 1          # counted as each frame ends


@cocotb.test()
async def test_window_larger_than_frame_is_black_outside(dut):
    """Frame smaller than the screen, centered: black borders on all sides."""
    w, h = 32, 4
    mem, csr, data = await setup(dut, w, h)
    await check(dut, csr, data, w, h, -16, -2, SHOW)
    # Rows and words outside the frame are never read from DDR.
    for addr, count in mem.bursts:
        assert BASE <= addr and addr + 8 * count <= BASE + w * h, (hex(addr), count)


@cocotb.test()
async def test_grey_bars_and_blank(dut):
    w, h = 64, 8
    _, csr, data = await setup(dut, w, h)
    await check(dut, csr, data, w, h, 0, 0, SHOW | GREY)
    await check(dut, csr, data, w, h, 0, 0, BARS)
    await check(dut, csr, data, w, h, 0, 0, 0)


@cocotb.test()
async def test_lut_maps_frame_not_bars(dut):
    """The LUT maps color and grey frames; bars and blanking bypass it.
    Before any write it is the identity."""
    w, h = 64, 8
    _, csr, data = await setup(dut, w, h)
    await check(dut, csr, data, w, h, 0, 0, SHOW | MAP)            # identity
    lut = [(255 - v) ^ 0x5A for v in range(256)]
    for i, v in enumerate(lut):
        await csr.write(LUT, i << 8 | v)
    await check(dut, csr, data, w, h, 0, 0, SHOW | MAP, lut)
    await check(dut, csr, data, w, h, 0, 0, SHOW | GREY | MAP, lut)
    await check(dut, csr, data, w, h, 0, 0, BARS | MAP)
    await check(dut, csr, data, w, h, 0, 0, MAP)                  # black, not lut[0]


@cocotb.test()
async def test_bursts_stay_inside_128_byte_blocks(dut):
    w, h = 200, 10                               # rows start at odd 8-byte offsets
    mem, csr, data = await setup(dut, w, h)
    await check(dut, csr, data, w, h, 8, 2, SHOW)
    for addr, count in mem.bursts:
        assert 1 <= count <= 16
        assert addr // 128 == (addr + 8 * count - 1) // 128, (hex(addr), count)


@cocotb.test()
async def test_slow_memory_counts_underflows(dut):
    """Memory too slow for the display: pairs show late, and are counted."""
    _, csr, _ = await setup(dut, 64, 8, stall=0.95, latency=(200, 400))
    await csr.write(CTRL, SHOW)
    await capture_frame(dut)
    await capture_frame(dut)
    assert await csr.read(UNDERFLOWS) > 0


@cocotb.test()
async def test_i2c_pins(dut):
    _, csr, _ = await setup(dut, 64, 8)
    await csr.write(I2C, 0b10)
    await ClockCycles(dut.clk, 2)
    assert (int(dut.hdmi_scl_oe.value), int(dut.hdmi_sda_oe.value)) == (0, 1)
    dut.hdmi_scl_in.value = 0
    dut.hdmi_int.value = 1
    await ClockCycles(dut.clk, 3)
    assert (await csr.read(I2C) >> 8) & 0x7 == 0b110      # INT=1, SDA=1, SCL=0
