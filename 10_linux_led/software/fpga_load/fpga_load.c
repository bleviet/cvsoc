// fpga_load.c - Kernel module to program Cyclone V FPGA via direct register access
// Bypasses the 10ms IRQ timeout in write_complete by using a polling loop.

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>

static char *firmware = "de10_nano.rbf";
module_param(firmware, charp, 0);
MODULE_PARM_DESC(firmware, "Firmware filename under /lib/firmware/");

/* FPGA Manager register offsets */
#define FPGAMGR_STAT        0x000
#define FPGAMGR_CTRL        0x004
#define FPGAMGR_DCLKCNT     0x008
#define FPGAMGR_DCLKSTAT    0x00C
#define FPGAMGR_GPIO_EXT    0x850

/* STAT: state in bits [2:0] */
#define STATE_MASK          0x7
#define STATE_RESET         0x1
#define STATE_CONFIG        0x2
#define STATE_INIT          0x3
#define STATE_USER_MODE     0x4

/* CTRL bits */
#define CTRL_EN             BIT(0)
#define CTRL_NCE            BIT(1)
#define CTRL_NCFGPULL       BIT(2)
#define CTRL_CDRATIO_MASK   (BIT(7)|BIT(6))
#define CTRL_AXICFGEN       BIT(8)
#define CTRL_CFGWDTH_MASK   BIT(9)
#define CDRATIO_X1          (0 << 6)
#define CDRATIO_X2          (1 << 6)
#define CDRATIO_X4          (2 << 6)
#define CDRATIO_X8          (3 << 6)
#define CFGWDTH_32          BIT(9)

/* GPIO_EXT bits */
#define MON_NSTATUS         BIT(0)
#define MON_CONF_DONE       BIT(1)
#define MON_INIT_DONE       BIT(2)
#define MON_CRC_ERROR       BIT(3)
#define MON_FPGA_POWER_ON   BIT(11)

/* MSEL[4:0] from STAT[8:3] */
#define MSEL_MASK           0xF8
#define MSEL_SHIFT          3

/* Physical base addresses */
#define FPGAMGR_PHYS        0xFF706000
#define FPGAMGR_SIZE        0x1000
#define FPGADAT_PHYS        0xFFB90000
#define FPGADAT_SIZE        0x4

/* Reset Manager and L3 GPV for bridge enable */
#define RSTMGR_BRGMODRST    0xFFD0501C
#define L3_REMAP            0xFF800000
#define BRGMODRST_LWH2F     BIT(1)
#define BRGMODRST_H2F       BIT(0)
#define BRGMODRST_F2H       BIT(2)
#define L3_REMAP_LWH2F      BIT(4)
#define L3_REMAP_H2F        BIT(3)

static inline u32 mgr_rd(void __iomem *base, u32 off)
{
    return readl(base + off);
}
static inline void mgr_wr(void __iomem *base, u32 off, u32 v)
{
    writel(v, base + off);
}

static int wait_state(void __iomem *base, u32 want_mask, int timeout_ms)
{
    int i;
    for (i = 0; i < timeout_ms * 10; i++) {
        u32 st = mgr_rd(base, FPGAMGR_STAT) & STATE_MASK;
        if (st & want_mask)
            return st;
        udelay(100);
    }
    return -ETIMEDOUT;
}

static int __init fpga_load_init(void)
{
    void __iomem *base, *data_port;
    const struct firmware *fw;
    struct device_node *node;
    struct platform_device *pdev;
    struct device *dev;
    u32 ctrl, msel, gpio;
    const u32 *buf32;
    size_t count, i, n;
    int ret, state;

    /* Get a device pointer for request_firmware */
    node = of_find_compatible_node(NULL, NULL, "altr,socfpga-fpga-mgr");
    if (!node) {
        pr_err("fpga_load: DT node not found\n");
        return -ENODEV;
    }
    pdev = of_find_device_by_node(node);
    of_node_put(node);
    dev = pdev ? &pdev->dev : NULL;

    /* Map FPGA Manager registers directly */
    base = ioremap(FPGAMGR_PHYS, FPGAMGR_SIZE);
    if (!base) {
        pr_err("fpga_load: ioremap FPGAMGR failed\n");
        return -ENOMEM;
    }
    data_port = ioremap(FPGADAT_PHYS, FPGADAT_SIZE);
    if (!data_port) {
        pr_err("fpga_load: ioremap FPGADAT failed\n");
        iounmap(base);
        return -ENOMEM;
    }

    msel = (mgr_rd(base, FPGAMGR_STAT) & MSEL_MASK) >> MSEL_SHIFT;
    gpio = mgr_rd(base, FPGAMGR_GPIO_EXT);
    pr_info("fpga_load: MSEL=0x%02X STAT=0x%08X GPIO=0x%08X\n",
            msel, mgr_rd(base, FPGAMGR_STAT), gpio);

    /* Step 1: Set CDRATIO=x8, CFGWDTH=32, clear NCE, set EN */
    ctrl = mgr_rd(base, FPGAMGR_CTRL);
    ctrl &= ~(CTRL_CDRATIO_MASK | CTRL_CFGWDTH_MASK | CTRL_NCE | CTRL_AXICFGEN);
    ctrl |= CDRATIO_X8 | CFGWDTH_32 | CTRL_EN;
    mgr_wr(base, FPGAMGR_CTRL, ctrl);

    /* Step 2: Assert NCFGPULL to reset FPGA */
    ctrl |= CTRL_NCFGPULL;
    mgr_wr(base, FPGAMGR_CTRL, ctrl);

    state = wait_state(base, STATE_RESET, 200);
    pr_info("fpga_load: after NCFGPULL: state=%d gpio=0x%08X\n",
            mgr_rd(base, FPGAMGR_STAT) & STATE_MASK, mgr_rd(base, FPGAMGR_GPIO_EXT));

    /* Step 3: Release NCFGPULL */
    ctrl &= ~CTRL_NCFGPULL;
    mgr_wr(base, FPGAMGR_CTRL, ctrl);

    state = wait_state(base, STATE_CONFIG, 200);
    pr_info("fpga_load: after release: state=%d (need %d=CONFIG)\n",
            mgr_rd(base, FPGAMGR_STAT) & STATE_MASK, STATE_CONFIG);
    if ((mgr_rd(base, FPGAMGR_STAT) & STATE_MASK) != STATE_CONFIG) {
        pr_err("fpga_load: FPGA did not enter CONFIG state\n");
        ret = -EIO;
        goto out;
    }

    /* Step 4: Load firmware */
    ret = request_firmware(&fw, firmware, dev);
    if (ret) {
        pr_err("fpga_load: request_firmware failed: %d\n", ret);
        goto out;
    }
    pr_info("fpga_load: firmware size=%zu bytes\n", fw->size);

    /* Step 5: Clear nSTATUS interrupt (EOI) then set AXICFGEN */
    mgr_wr(base, 0x84C, 0xFFF);  /* GPIO_PORTA_EOI: clear all pending */
    ctrl |= CTRL_AXICFGEN;
    mgr_wr(base, FPGAMGR_CTRL, ctrl);

    /* Step 6: Write bitstream */
    buf32 = (const u32 *)fw->data;
    count = fw->size;
    n = count / 4;
    for (i = 0; i < n; i++)
        writel(buf32[i], data_port);
    /* Handle remaining bytes */
    if (count & 3) {
        u32 v = 0;
        memcpy(&v, fw->data + n * 4, count & 3);
        writel(v, data_port);
    }
    release_firmware(fw);
    pr_info("fpga_load: write done, polling CONF_DONE (5s max, AXICFGEN=1)...\n");

    /* Step 7: Poll GPIO_EXT for CONF_DONE (AXICFGEN must remain set!) */
    for (i = 0; i < 5000; i++) {
        gpio = mgr_rd(base, FPGAMGR_GPIO_EXT);
        if (gpio & MON_CONF_DONE) {
            pr_info("fpga_load: CONF_DONE! gpio=0x%08X at i=%zu\n", gpio, i);
            break;
        }
        if (gpio & MON_CRC_ERROR) {
            pr_err("fpga_load: CRC_ERROR! gpio=0x%08X\n", gpio);
            ret = -EIO;
            goto out;
        }
        if (!(gpio & MON_NSTATUS)) {
            pr_err("fpga_load: nSTATUS LOW (error)! gpio=0x%08X\n", gpio);
            ret = -EIO;
            goto out;
        }
        msleep(1);
    }

    gpio = mgr_rd(base, FPGAMGR_GPIO_EXT);
    if (!(gpio & MON_CONF_DONE)) {
        pr_err("fpga_load: TIMEOUT - CONF_DONE never asserted. gpio=0x%08X STAT=0x%08X\n",
               gpio, mgr_rd(base, FPGAMGR_STAT));
        ret = -ETIMEDOUT;
        goto out;
    }

    /* Step 8: Clear AXICFGEN now that CONF_DONE asserted */
    ctrl &= ~CTRL_AXICFGEN;
    mgr_wr(base, FPGAMGR_CTRL, ctrl);

    /* Step 9: Send DCLK pulses and wait for USER_MODE */
    mgr_wr(base, FPGAMGR_DCLKSTAT, 1); /* clear */
    mgr_wr(base, FPGAMGR_DCLKCNT, 4);
    msleep(10);

    state = wait_state(base, STATE_USER_MODE, 500);
    pr_info("fpga_load: final state=%d (need %d=USER_MODE)\n",
            mgr_rd(base, FPGAMGR_STAT) & STATE_MASK, STATE_USER_MODE);

    if ((mgr_rd(base, FPGAMGR_STAT) & STATE_MASK) != STATE_USER_MODE) {
        pr_err("fpga_load: FPGA did not reach USER_MODE\n");
        ret = -EIO;
        goto out;
    }

    /* Step 10: Clear EN */
    ctrl &= ~CTRL_EN;
    mgr_wr(base, FPGAMGR_CTRL, ctrl);
    pr_info("fpga_load: FPGA programmed successfully!\n");

    /* Step 11: Enable LW H2F bridge (cycle reset + set L3 remap) */
    {
        void __iomem *rstmgr = ioremap(RSTMGR_BRGMODRST, 4);
        void __iomem *l3remap = ioremap(L3_REMAP, 4);
        if (rstmgr && l3remap) {
            u32 rst = readl(rstmgr);
            /* Assert bridge resets */
            writel(rst | BRGMODRST_LWH2F | BRGMODRST_H2F, rstmgr);
            udelay(100);
            /* Deassert bridge resets */
            writel(rst & ~(BRGMODRST_LWH2F | BRGMODRST_H2F), rstmgr);
            udelay(100);
            /* Enable LW H2F in L3 remap */
            writel(readl(l3remap) | L3_REMAP_LWH2F | L3_REMAP_H2F, l3remap);
            pr_info("fpga_load: LW H2F bridge enabled\n");
        }
        if (rstmgr) iounmap(rstmgr);
        if (l3remap) iounmap(l3remap);
    }
    ret = 0;

out:
    iounmap(data_port);
    iounmap(base);
    return ret;
}

static void __exit fpga_load_exit(void)
{
    pr_info("fpga_load: module unloaded\n");
}

module_init(fpga_load_init);
module_exit(fpga_load_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Load FPGA firmware via direct FPGA Manager register access");
MODULE_AUTHOR("cvsoc");
