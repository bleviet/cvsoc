################################################################################
#
# led-opcua-server — Industrial OPC UA server for FPGA LED control on DE10-Nano
#
################################################################################

LED_OPCUA_SERVER_VERSION = 1.0
LED_OPCUA_SERVER_SITE = $(BR2_EXTERNAL_DE10_NANO_PATH)/../software/opcua_server
LED_OPCUA_SERVER_SITE_METHOD = local
LED_OPCUA_SERVER_LICENSE = MIT

define LED_OPCUA_SERVER_BUILD_CMDS
	$(MAKE) $(TARGET_CONFIGURE_OPTS) -C $(@D)
endef

define LED_OPCUA_SERVER_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/led_opcua_server $(TARGET_DIR)/usr/bin/led_opcua_server
endef

$(eval $(generic-package))
