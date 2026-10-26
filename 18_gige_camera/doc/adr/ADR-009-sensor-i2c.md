# ADR-009: camd configures the sensor by bit-banging I2C through a CSR

- **Status**: Accepted (M2). Revisit in M5 (V4L2 driver).
- **Date**: 2026-10-26
- **Plan**: [GigE Vision camera product plan](../../../docs/plans/2026-10-25-gige-vision-camera.md), §5 (`i2c_master`), §6

## Context

The MT9P001 on the TRDB-D5M is configured over I2C (SCLK/SDATA on GPIO
header JP1). The plan proposed the OpenCores I2C core in the FPGA so that
Linux could drive it with the upstream `i2c-ocores` driver.

The board runs the `16_field_update` image (kernel 6.6.86). Read on the
board, 2026-10-26:

- `CONFIG_I2C_OCORES` and `CONFIG_I2C_GPIO` are not set.
- `CONFIG_OF_OVERLAY=y`, but `CONFIG_CONFIGFS_FS` is not set, so there is no
  way to load a device-tree overlay from user space.

A kernel I2C master therefore needs a new kernel and device tree, which is
the `18_gige_camera` Buildroot image planned for M5. M2 only has to write a
dozen sensor registers when acquisition starts.

## Decision

`cam_frame_writer` exposes the two lines as open-drain bits in its `I2C`
register (pull SCLK low, pull SDATA low, read both pin levels). camd
bit-bangs I2C through it (`software/camd/i2c_bitbang.c`) at about 100 kHz,
busy-waiting the half periods. `software/camd/mt9p001.c` holds the sensor's
register map and timing model on top of that.

## Consequences

- No kernel or device-tree change: M2 runs on the existing image.
- camd is the only bus master. Nothing else (a kernel driver, `i2c-tools`)
  may touch the sensor while camd runs.
- A configuration (12 register writes) takes a few milliseconds of busy
  waiting in the stream thread when acquisition starts or the ROI changes.
  That is fine for configuration. M4's auto-exposure writes exposure and gain
  every frame, so it will spend CPU time on every frame: measure it then.
- The I2C timing depends on how fast the LW bridge and the CPU are, not on
  hardware. The MT9P001 accepts up to 400 kHz, so there is margin.
- **M5:** the V4L2 driver needs a kernel I2C adapter. Add the OpenCores core
  (or `i2c-gpio` on these same pins) with the new image and device tree, and
  move the MT9P001 configuration into the kernel's `mt9p031` driver, which
  supports this sensor.
