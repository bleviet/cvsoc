################################################################################
#
# fpga-mgr-load — Kernel module to load FPGA bitstream via kernel FPGA Manager
#
# Programs the Cyclone V FPGA from /lib/firmware/de10_nano.rbf using the
# kernel's proven fpga_mgr_load() code path.
#
################################################################################

FPGA_MGR_LOAD_VERSION = 1.0
FPGA_MGR_LOAD_SITE = $(BR2_EXTERNAL_DE10_NANO_PATH)/../software/fpga_load
FPGA_MGR_LOAD_SITE_METHOD = local
FPGA_MGR_LOAD_LICENSE = GPL-2.0

$(eval $(kernel-module))
$(eval $(generic-package))
