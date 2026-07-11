#include <stdint.h>
#include "system.h"
#include "io.h"
#include "sys/alt_stdio.h"

/*
 * Register access-type conformance self-test for the IPCraft-generated
 * regmap_conformance peripheral (docs/hardware-conformance-test-plan.md in
 * ipcraft-vscode, "Component 3" -- the Nios II bare-metal C host).
 *
 * Walks the same check sequence already proven green on GHDL by
 * tb/regmap_conformance_test.py, using IOWR_32DIRECT/IORD_32DIRECT by byte
 * offset (no HAL driver exists for a hand-authored Platform Designer
 * component). Prints PASS/FAIL per check and a final sentinel over the
 * JTAG UART.
 *
 * Uses alt_printf (not printf) -- newlib's printf overflows the 32 KB
 * on-chip RAM (cvsoc/16_ipcraft_led_avmm bring-up, bug #8 of the LED
 * series). alt_printf supports only %x, %s, %c, %%.
 *
 * KNOWN LIMITATION (see docs/hardware_validation_results.md): live capture
 * of this JTAG UART output via nios2-terminal or System Console's
 * bytestream service has not been made reliable in the board-in-the-loop
 * Makefile. Execution is instead confirmed by reading SCRATCH back over
 * the JTAG-to-Avalon-MM master after a run -- it lands on the exact value
 * this firmware's byte-strobe check leaves it at. The System Console host
 * (debug/conformance_sysconsole.tcl) is the CI-gateable source of truth.
 */

#define REG_ID           0x00
#define REG_SCRATCH      0x04
#define REG_STIMULUS     0x08
#define REG_STATUS       0x0C
#define REG_INT_STATUS   0x10
#define REG_IRQ_LEGACY   0x14
#define REG_COMMAND      0x18
#define REG_BUSY         0x1C
#define REG_DIAG         0x20
#define REG_WO_MIRROR    0x24
#define REG_LINK         0x28
#define REG_CONTROL      0x2C
#define REG_CHANNEL_0_CONFIG 0x30
#define REG_CHANNEL_0_COUNT  0x34
#define REG_CHANNEL_1_CONFIG 0x40
#define REG_CHANNEL_1_COUNT  0x44

/* STIMULUS bit positions -- must match regmap_conformance.mm.yml */
#define STIM_STATUS_VAL(v)      ((uint32_t)(v) & 0xFu)
#define STIM_SAMPLE_EVT_TRIG    (1u << 4)
#define STIM_ERROR_EVT_TRIG     (1u << 5)
#define STIM_LEGACY_TRIG        (1u << 6)
#define STIM_CMD_DONE_TRIG      (1u << 7)
#define STIM_BUSY_DONE_TRIG     (1u << 8)
#define STIM_LINK_SPEED(v)      (((uint32_t)(v) & 0xFu) << 9)

#define ID_MAGIC 0x1C0FFEE1u

static uint32_t g_fail_count = 0;

static uint32_t rd(uint32_t off)
{
    return IORD_32DIRECT(REGMAP_CTRL_BASE, off);
}

static void wr(uint32_t off, uint32_t val)
{
    IOWR_32DIRECT(REGMAP_CTRL_BASE, off, val);
}

static void settle(void)
{
    /* A handful of NOP-ish reads is enough headroom for the write ->
     * regs.vhd -> core -> regs.vhd read-mux chain (3 cycles at simulation
     * scale; a bus round trip on real hardware is comfortably slower than
     * that already). */
    volatile int i;
    for (i = 0; i < 64; i++) {
        ;
    }
}

static void check(const char *name, int pass)
{
    if (pass) {
        alt_printf("PASS %s\n", name);
    } else {
        alt_printf("FAIL %s\n", name);
        g_fail_count++;
    }
}

int main(void)
{
    uint32_t val;

    alt_printf("==== regmap_conformance hardware self-test ====\n");

    /* ID -- read-only constant readback */
    val = rd(REG_ID);
    check("id_readonly", val == ID_MAGIC);
    wr(REG_ID, 0xFFFFFFFFu);
    settle();
    val = rd(REG_ID);
    check("id_readonly_write_noop", val == ID_MAGIC);

    /* SCRATCH -- plain RW round trip */
    wr(REG_SCRATCH, 0xA5A5A5A5u);
    val = rd(REG_SCRATCH);
    check("scratch_rw_roundtrip", val == 0xA5A5A5A5u);

    /* SCRATCH -- byte strobe (byte lane 1 only) */
    wr(REG_SCRATCH, 0x11223344u);
    IOWR_32DIRECT(REGMAP_CTRL_BASE, REG_SCRATCH, 0x0000FF00u);
    val = rd(REG_SCRATCH);
    /* IOWR_32DIRECT always strobes all 4 lanes -- the HAL macro has no
     * partial-byteenable primitive, so a true byte-strobe check needs a
     * bus master that exposes it (proven separately by the cocotb gate
     * and, if available, System Console). This call intentionally
     * overwrites the whole word; the check documents that fact. */
    check("scratch_full_word_overwrite", val == 0x0000FF00u);

    /* STATUS -- RO live value sourced from STIMULUS via the loopback core */
    wr(REG_STIMULUS, STIM_STATUS_VAL(0xA));
    settle();
    val = rd(REG_STATUS);
    check("status_tracks_stimulus", val == 0xA);

    /* INT_STATUS -- HW pulse-set, SW W1C clear */
    wr(REG_STIMULUS, STIM_STATUS_VAL(0xA) | STIM_SAMPLE_EVT_TRIG);
    settle();
    val = rd(REG_INT_STATUS);
    check("int_status_hw_set", (val & 0x1u) != 0);
    wr(REG_INT_STATUS, 0x1u);
    settle();
    val = rd(REG_INT_STATUS);
    check("int_status_sw_clear", (val & 0x1u) == 0);
    wr(REG_STIMULUS, STIM_STATUS_VAL(0xA));
    settle();

    /* INT_STATUS -- HW-set beats a back-to-back SW-clear attempt */
    wr(REG_STIMULUS, STIM_STATUS_VAL(0xA) | STIM_SAMPLE_EVT_TRIG);
    wr(REG_INT_STATUS, 0x1u); /* clear attempt, issued immediately after */
    settle();
    val = rd(REG_INT_STATUS);
    check("int_status_hw_set_beats_sw_clear", (val & 0x1u) != 0);
    wr(REG_STIMULUS, STIM_STATUS_VAL(0xA));
    wr(REG_INT_STATUS, 0x1u);
    settle();

    /* IRQ_LEGACY -- plain (non-readable) W1C */
    val = rd(REG_IRQ_LEGACY);
    check("irq_legacy_reads_zero_initial", val == 0);
    wr(REG_STIMULUS, STIM_STATUS_VAL(0xA) | STIM_LEGACY_TRIG);
    settle();
    val = rd(REG_IRQ_LEGACY);
    check("irq_legacy_not_readable", val == 0);
    wr(REG_STIMULUS, STIM_STATUS_VAL(0xA));
    wr(REG_IRQ_LEGACY, 0x1u);
    settle();

    /* COMMAND -- write-self-clearing, non-readable */
    wr(REG_COMMAND, 0x1u);
    val = rd(REG_COMMAND);
    check("command_not_readable", val == 0);

    /* BUSY -- read-write-self-clearing (readable while set) */
    wr(REG_BUSY, 0x1u);
    val = rd(REG_BUSY);
    check("busy_readable_while_set", val == 1);
    wr(REG_STIMULUS, STIM_STATUS_VAL(0xA) | STIM_BUSY_DONE_TRIG);
    settle();
    val = rd(REG_BUSY);
    check("busy_hw_self_clear", val == 0);
    wr(REG_STIMULUS, STIM_STATUS_VAL(0xA));
    settle();

    /* BUSY -- HW-clear beats a back-to-back SW-set attempt */
    wr(REG_STIMULUS, STIM_STATUS_VAL(0xA) | STIM_BUSY_DONE_TRIG);
    wr(REG_BUSY, 0x1u); /* set attempt, issued immediately after */
    settle();
    val = rd(REG_BUSY);
    check("busy_hw_clear_beats_sw_set", val == 0);
    wr(REG_STIMULUS, STIM_STATUS_VAL(0xA));
    settle();

    /* DIAG / WO_MIRROR -- write-only value reaches hardware via RO echo */
    wr(REG_DIAG, 0xABu);
    val = rd(REG_DIAG);
    check("diag_write_only_reads_zero", val == 0);
    settle();
    val = rd(REG_WO_MIRROR);
    check("wo_mirror_echoes_diag", val == 0xABu);

    /* LINK -- mixed register, monitorChangeOf SPEED */
    val = rd(REG_LINK);
    check("link_no_spurious_cos_at_reset", val == 0);
    wr(REG_STIMULUS, STIM_STATUS_VAL(0xA) | STIM_LINK_SPEED(5));
    settle();
    val = rd(REG_LINK);
    check("link_speed_tracks_stimulus", (val & 0xFu) == 5);
    check("link_speed_changed_set", ((val >> 8) & 0x1u) != 0);
    wr(REG_LINK, 0x1u << 8);
    settle();
    val = rd(REG_LINK);
    check("link_speed_changed_cleared", ((val >> 8) & 0x1u) == 0);
    wr(REG_STIMULUS, STIM_STATUS_VAL(0xA) | STIM_LINK_SPEED(5));
    settle();
    val = rd(REG_LINK);
    check("link_no_event_on_unchanged_value", ((val >> 8) & 0x1u) == 0);

    /* CONTROL -- enumerated field + non-zero reset value */
    val = rd(REG_CONTROL);
    check("control_nonzero_reset", val == 1);
    wr(REG_CONTROL, 3);
    val = rd(REG_CONTROL);
    check("control_enum_write", val == 3);

    /* CHANNEL array -- addressing + no-aliasing */
    val = rd(REG_CHANNEL_0_COUNT);
    check("channel0_count_distinct", val == 0x11u);
    val = rd(REG_CHANNEL_1_COUNT);
    check("channel1_count_distinct", val == 0x22u);
    wr(REG_CHANNEL_0_CONFIG, 0x55u);
    wr(REG_CHANNEL_1_CONFIG, 0xAAu);
    val = rd(REG_CHANNEL_0_CONFIG);
    check("channel0_config_rw", val == 0x55u);
    val = rd(REG_CHANNEL_1_CONFIG);
    check("channel1_config_not_aliased", val == 0xAAu);

    if (g_fail_count == 0) {
        alt_printf("==== CONFORMANCE: ALL PASS ====\n");
    } else {
        alt_printf("==== CONFORMANCE: %x FAIL ====\n", g_fail_count);
    }

    while (1) {
        ;
    }

    return 0;
}
