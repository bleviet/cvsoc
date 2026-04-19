// fpga_load.c - Kernel module to program Cyclone V FPGA via FPGA Manager
// Usage: insmod fpga_load.ko firmware=de10_nano.rbf
//
// Calls the kernel's proven fpga_mgr_load() path (same as the FPGA region driver)

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fpga/fpga-mgr.h>
#include <linux/of.h>
#include <linux/firmware.h>
#include <linux/device.h>

static char *firmware = "de10_nano.rbf";
module_param(firmware, charp, 0);
MODULE_PARM_DESC(firmware, "Firmware filename under /lib/firmware/");

static int __init fpga_load_init(void)
{
    struct device_node *node;
    struct fpga_manager *mgr;
    struct fpga_image_info *info;
    const struct firmware *fw;
    int ret;

    pr_info("fpga_load: loading firmware '%s'\n", firmware);

    /* Find the FPGA Manager device node */
    node = of_find_compatible_node(NULL, NULL, "altr,socfpga-fpga-mgr");
    if (!node) {
        pr_err("fpga_load: could not find altr,socfpga-fpga-mgr in DT\n");
        return -ENODEV;
    }

    mgr = of_fpga_mgr_get(node);
    of_node_put(node);
    if (IS_ERR(mgr)) {
        pr_err("fpga_load: failed to get fpga_manager: %ld\n", PTR_ERR(mgr));
        return PTR_ERR(mgr);
    }

    pr_info("fpga_load: got fpga_manager '%s', state=%d\n",
            mgr->name, mgr->state);

    /* Allocate image info */
    info = fpga_image_info_alloc(&mgr->dev);
    if (!info) {
        fpga_mgr_put(mgr);
        return -ENOMEM;
    }
    info->flags = 0;  /* Full reconfiguration */

    /* Load firmware from /lib/firmware/ */
    ret = request_firmware(&fw, firmware, &mgr->dev);
    if (ret) {
        pr_err("fpga_load: request_firmware('%s') failed: %d\n", firmware, ret);
        fpga_image_info_free(info);
        fpga_mgr_put(mgr);
        return ret;
    }

    pr_info("fpga_load: firmware size = %zu bytes\n", fw->size);

    /* Set the buffer in the image info */
    info->buf = fw->data;
    info->count = fw->size;

    /* Lock and load */
    ret = fpga_mgr_lock(mgr);
    if (ret) {
        pr_err("fpga_load: fpga_mgr_lock failed: %d\n", ret);
        goto out_fw;
    }

    ret = fpga_mgr_load(mgr, info);
    fpga_mgr_unlock(mgr);

    if (ret)
        pr_err("fpga_load: fpga_mgr_load failed: %d\n", ret);
    else
        pr_info("fpga_load: FPGA programmed successfully!\n");

out_fw:
    release_firmware(fw);
    fpga_image_info_free(info);
    fpga_mgr_put(mgr);
    return ret;
}

static void __exit fpga_load_exit(void)
{
    pr_info("fpga_load: module unloaded\n");
}

module_init(fpga_load_init);
module_exit(fpga_load_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Load FPGA firmware via kernel FPGA Manager");
MODULE_AUTHOR("cvsoc");
