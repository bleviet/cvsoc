"""Golden model of the cvsoc test-pattern generator.

Must match tpg_pixel() in software/camd/source_sim.c and, from M1 on, the
FPGA test-pattern generator. (x, y) are sensor coordinates.
"""

OFF, GREY_HRAMP_MOVING, COLOR_BAR = 0, 1, 2
MONO8, BAYER_GR8 = 0x01080001, 0x01080008

COLOR_BARS = [(255, 255, 255), (255, 255, 0), (0, 255, 255), (0, 255, 0),
              (255, 0, 255), (255, 0, 0), (0, 0, 255), (0, 0, 0)]


def pixel(pattern, pixel_format, x, y, n, sensor_width):
    if pattern == GREY_HRAMP_MOVING:
        return (x + n) & 0xFF
    if pattern == COLOR_BAR:
        r, g, b = COLOR_BARS[x * 8 // sensor_width]
        if pixel_format == MONO8:
            return (77 * r + 150 * g + 29 * b) >> 8
        if y % 2 == 0:
            return r if x % 2 else g        # G R
        return g if x % 2 else b            # B G
    return 0


def frame(pattern, pixel_format, width, height, offset_x, offset_y, n, sensor_width):
    """Frame `n` of the pattern as bytes, row-major."""
    return bytes(pixel(pattern, pixel_format, x + offset_x, y + offset_y, n, sensor_width)
                 for y in range(height) for x in range(width))
