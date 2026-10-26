/*
 * adv7513.h — the DE10-Nano's ADV7513 HDMI transmitter, configured over I2C.
 *
 * The register values are those of Terasic's DE10-Nano HDMI_TX demo
 * (I2C_HDMI_Config.v): 24-bit RGB 4:4:4 input with separate syncs, HDMI
 * output, color space conversion off. The ADV7513 resets its registers and
 * powers down while hot-plug detect is low, so camd calls adv7513_poll()
 * periodically to configure it again after a monitor is plugged in.
 */
#ifndef ADV7513_H
#define ADV7513_H

#include "i2c_bitbang.h"

#define ADV7513_I2C_ADDR 0x39           /* 0x72/0x73 on the bus */

/* Configure the transmitter. Returns 0, or -1 if it did not acknowledge. */
int adv7513_init(struct i2c_bus *bus);

/* Configure the transmitter if a monitor is attached and it is powered down.
 * Returns 1 if it was (re)configured, 0 if nothing was needed, -1 on error.
 * *hpd is set to the hot-plug detect state (1: a monitor is attached). */
int adv7513_poll(struct i2c_bus *bus, int *hpd);

#endif /* ADV7513_H */
