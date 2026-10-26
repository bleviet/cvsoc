/*
 * mt9p001.h — the TRDB-D5M's MT9P001 sensor: register map, timing model and
 * configuration over I2C.
 *
 * Timing (MT9P001 datasheet, "Frame Time and Exposure"), in pixel clocks:
 *   row time    tROW   = 2 * LROW,  LROW = W/2 + max(HB, HBmin)
 *   frame time  tFRAME = (H + max(VB, VBmin)) * tROW
 *   exposure    tEXP   = SW * tROW - 2 * SO
 * with HB = Horizontal_Blank + 1, VB = Vertical_Blank + 1, SW the shutter
 * width in rows and SO = 398 the shutter overhead (no binning). HBmin is
 * measured on the board rather than taken from the datasheet: source_fpga.c
 * reads the line period of a full-width frame at start-up.
 */
#ifndef MT9P001_H
#define MT9P001_H

#include <stdint.h>

#include "i2c_bitbang.h"

#define MT9P001_I2C_ADDR        0x5D    /* 0xBA/0xBB on the bus (SADDR high) */

/* Registers */
#define MT9P001_CHIP_VERSION    0x00
#define MT9P001_ROW_START       0x01
#define MT9P001_COLUMN_START    0x02
#define MT9P001_ROW_SIZE        0x03
#define MT9P001_COLUMN_SIZE     0x04
#define MT9P001_HORZ_BLANK      0x05
#define MT9P001_VERT_BLANK      0x06
#define MT9P001_OUTPUT_CONTROL  0x07
#define MT9P001_SHUTTER_UPPER   0x08
#define MT9P001_SHUTTER_LOWER   0x09
#define MT9P001_PIXEL_CLOCK     0x0A
#define MT9P001_RESTART         0x0B
#define MT9P001_READ_MODE_1     0x1E
#define MT9P001_READ_MODE_2     0x20
#define MT9P001_ROW_ADDR_MODE   0x22
#define MT9P001_COL_ADDR_MODE   0x23
#define MT9P001_GLOBAL_GAIN     0x35
#define MT9P001_ROW_BLACK_OFFSET 0x4B
#define MT9P001_TEST_PATTERN    0xA0    /* MT9P031-style test pattern block */
#define MT9P001_TEST_GREEN      0xA1
#define MT9P001_TEST_RED        0xA2
#define MT9P001_TEST_BLUE       0xA3

/* Output_Control: Chip_Enable, slowest DOUT edges (bits 12:10 = 0), PIXCLK
 * edges at the default (bits 9:7 = 7). Rev C of the datasheet does not
 * document bits 12:7; MT9P031-family sensors use them for the output and
 * PIXCLK slew rates. On the D5M + JP1, any faster DOUT setting glitched
 * LINE_VALID/PIXCLK in every frame when all 12 lines switch together
 * (measured 2026-10-26 with the 0xFFF color field: doc/README.md, M2). */
#define MT9P001_OUTPUT_CONTROL_RUN      0x0382
#define MT9P001_SYNCHRONIZE_CHANGES     0x0001
#define MT9P001_INVERT_PIXEL_CLOCK      0x8000
#define MT9P001_READ_MODE_2_DEFAULT     0x0040   /* Row_BLC on */
#define MT9P001_ROW_BLACK_OFFSET_DEFAULT 0x0028

/* The 2592 x 1944 active image starts at column 16, row 54 (datasheet
 * Tables 4 and 5). Its first row reads G1 R G1 R..., so the CFA is GR. */
#define MT9P001_COLUMN_START_ACTIVE 16
#define MT9P001_ROW_START_ACTIVE    54

#define MT9P001_SHUTTER_OVERHEAD    398
#define MT9P001_HB_MAX              4096    /* Horizontal_Blank + 1 */
#define MT9P001_VB_MIN              9       /* Vertical_Blank + 1 */
#define MT9P001_VB_MAX              2048
#define MT9P001_HBMIN_DEFAULT       450     /* until measured */

struct mt9p001_mode {
    uint32_t width, height, offset_x, offset_y;   /* in active-image pixels */
    double   frame_rate;                          /* requested, Hz */
    double   exposure_us;                         /* requested */
    double   gain;                                /* 1.0 .. 15.75 */
    double   pixclk_hz;
    uint32_t hb_min;                              /* HBmin in ACLKs */
};

struct mt9p001_regs {
    uint16_t row_start, column_start, row_size, column_size;
    uint16_t horz_blank, vert_blank, shutter_upper, shutter_lower, gain;
    /* What the registers give: */
    double   frame_rate, exposure_us;
    uint32_t line_pixclks;                        /* tROW in pixel clocks */
};

/* Pure: register values for a mode (frame rate and exposure clamped to what
 * the sensor can do; the exposure never extends the frame). */
void   mt9p001_compute(const struct mt9p001_mode *m, struct mt9p001_regs *r);
double mt9p001_max_frame_rate(uint32_t width, uint32_t height, double pixclk_hz,
                              uint32_t hb_min);

/* I2C. All return 0, or -1 if the sensor did not acknowledge. */
int mt9p001_probe(struct i2c_bus *bus, uint16_t *chip_version);
int mt9p001_setup(struct i2c_bus *bus);           /* once after reset */
int mt9p001_apply(struct i2c_bus *bus, const struct mt9p001_regs *r);
/* Test pattern: on = 0 restores the image; otherwise every pixel = value.
 * The pattern enters the data path ahead of the digital black-level
 * correction, so Row BLC (which would turn every value into
 * Row_Black_Target) and the default black offset are switched off while it
 * is on. */
int mt9p001_color_field(struct i2c_bus *bus, int on, uint16_t value);

#endif /* MT9P001_H */
