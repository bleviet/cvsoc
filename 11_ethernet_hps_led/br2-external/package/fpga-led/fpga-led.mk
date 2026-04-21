################################################################################
#
# fpga-led — User-space FPGA LED controller for DE10-Nano
#
# Sources live in Phase 6 (10_linux_led) and are reused unchanged in Phase 7.
#
################################################################################

FPGA_LED_VERSION = 1.0
FPGA_LED_SITE = $(BR2_EXTERNAL_DE10_NANO_PATH)/../../10_linux_led/software/fpga_led
FPGA_LED_SITE_METHOD = local
FPGA_LED_LICENSE = MIT

define FPGA_LED_BUILD_CMDS
	$(MAKE) $(TARGET_CONFIGURE_OPTS) -C $(@D)
endef

define FPGA_LED_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/fpga_led $(TARGET_DIR)/usr/bin/fpga_led
endef

$(eval $(generic-package))
