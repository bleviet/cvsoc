################################################################################
#
# led-server — UDP server for remote FPGA LED control on DE10-Nano
#
################################################################################

LED_SERVER_VERSION = 1.0
LED_SERVER_SITE = $(BR2_EXTERNAL_DE10_NANO_PATH)/../software/led_server
LED_SERVER_SITE_METHOD = local
LED_SERVER_LICENSE = MIT

define LED_SERVER_BUILD_CMDS
	$(MAKE) $(TARGET_CONFIGURE_OPTS) -C $(@D)
endef

define LED_SERVER_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/led_server $(TARGET_DIR)/usr/bin/led_server
endef

$(eval $(generic-package))
