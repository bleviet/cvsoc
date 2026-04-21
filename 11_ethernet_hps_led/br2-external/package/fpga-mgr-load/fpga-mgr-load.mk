################################################################################
#
# fpga-mgr-load — Kernel module to load FPGA bitstream via kernel FPGA Manager
#
# Sources live in Phase 6 (10_linux_led) and are reused unchanged in Phase 7.
#
################################################################################

FPGA_MGR_LOAD_VERSION = 1.0
FPGA_MGR_LOAD_SITE = $(BR2_EXTERNAL_DE10_NANO_PATH)/../../10_linux_led/software/fpga_load
FPGA_MGR_LOAD_SITE_METHOD = local
FPGA_MGR_LOAD_LICENSE = GPL-2.0

$(eval $(kernel-module))
$(eval $(generic-package))
