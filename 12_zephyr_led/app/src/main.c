/*
 * main.c — Phase 8: Zephyr RTOS LED demo on DE10-Nano HPS
 *
 * Runs on the ARM Cortex-A9 Hard Processor System (HPS) and controls
 * the 8 FPGA LEDs through the Lightweight HPS-to-FPGA bridge at 0xFF200000.
 *
 * Architecture:
 *
 *   ┌──────────────────────┐      ┌─────────────────────────────┐
 *   │  led_pattern_thread  │      │  button_monitor_thread       │
 *   │  (prio 5, 2 KB stack)│      │  (prio 4, 1 KB stack)        │
 *   │                      │      │                               │
 *   │  Cycles LED patterns │      │  Configures KEY[0]/KEY[1]     │
 *   │  at ~500 ms per step │      │  gpio interrupts              │
 *   │  using sys_write32() │      │  Posts btn_sem on press       │
 *   │  to FPGA PIO register│      └──────────────┬────────────────┘
 *   │                      │                     │ k_sem_give()
 *   │  Waits on pattern_sem│◄────────────────────┘
 *   │  to change pattern   │
 *   └──────────────────────┘
 *
 * LED patterns (selected by button press, cycling forward):
 *
 *   0 CHASE   — single LED running left → right (0x01 → 0x02 → … → 0x80)
 *   1 BREATHE — LEDs fill up then drain  (0x01 → 0x03 → … → 0xFF → 0x7F → …)
 *   2 BLINK   — all 8 LEDs on/off        (0xFF → 0x00)
 *   3 STRIPES — alternating checkerboard (0xAA → 0x55)
 *
 * Hardware memory map:
 *   0xFF200000  Avalon PIO DATA register (8-bit LED output via LW H2F bridge)
 *
 * Build:
 *   cd zephyrproject   (west workspace — created by 'make init')
 *   west build -b cyclonev_socdk ../app
 *
 * Flash:
 *   west flash
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/devicetree.h>

LOG_MODULE_REGISTER(zephyr_led, LOG_LEVEL_INF);

/* ── FPGA LED PIO register ───────────────────────────────────────────────── */
/*
 * Physical address of the Avalon PIO DATA register on the Lightweight
 * HPS-to-FPGA bridge.  Matches LWH2F_BASE used in all previous phases.
 */
#define LED_PIO_ADDR   DT_REG_ADDR(DT_NODELABEL(fpga_leds))

static inline void led_write(uint8_t pattern)
{
	sys_write32((uint32_t)pattern, LED_PIO_ADDR);
}

static inline uint8_t led_read(void)
{
	return (uint8_t)sys_read32(LED_PIO_ADDR);
}

/* ── LED pattern definitions ─────────────────────────────────────────────── */
typedef enum {
	PATTERN_CHASE   = 0,
	PATTERN_BREATHE = 1,
	PATTERN_BLINK   = 2,
	PATTERN_STRIPES = 3,
	PATTERN_COUNT,
} pattern_id_t;

static const char *const pattern_names[PATTERN_COUNT] = {
	[PATTERN_CHASE]   = "CHASE",
	[PATTERN_BREATHE] = "BREATHE",
	[PATTERN_BLINK]   = "BLINK",
	[PATTERN_STRIPES] = "STRIPES",
};

/* ── Shared state ─────────────────────────────────────────────────────────── */
static volatile pattern_id_t current_pattern = PATTERN_CHASE;
static K_SEM_DEFINE(pattern_sem, 0, 1);   /* posted by button ISR */

/* ── Push-button GPIO setup ─────────────────────────────────────────────── */
static const struct gpio_dt_spec btn0 =
	GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);

static const struct gpio_dt_spec btn1 =
	GPIO_DT_SPEC_GET(DT_ALIAS(sw1), gpios);

static struct gpio_callback btn0_cb_data;
static struct gpio_callback btn1_cb_data;

/*
 * Button ISR — runs in interrupt context.
 * Simply advances the pattern and posts the semaphore; all real work
 * happens in button_monitor_thread().
 */
static void button_pressed(const struct device *dev,
			   struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	pattern_id_t next = (current_pattern + 1) % PATTERN_COUNT;
	current_pattern = next;
	k_sem_give(&pattern_sem);
}

/* ── LED pattern thread ─────────────────────────────────────────────────── */
/*
 * Generates the current LED animation frame-by-frame.
 * Wakes to check for pattern changes via 'pattern_sem'.
 */
static void led_pattern_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	const uint32_t step_ms = 120; /* ms between animation frames */
	uint8_t step = 0;

	LOG_INF("LED pattern thread started");
	LOG_INF("FPGA LED PIO at 0x%08X", LED_PIO_ADDR);
	LOG_INF("Initial LED state: 0x%02X", led_read());

	while (true) {
		pattern_id_t pat = current_pattern;

		/* Check for pattern change — non-blocking */
		if (k_sem_take(&pattern_sem, K_NO_WAIT) == 0) {
			pat = current_pattern;
			step = 0;
			LOG_INF("Pattern changed → %s", pattern_names[pat]);
		}

		/* Generate next animation frame */
		uint8_t out = 0;
		switch (pat) {
		case PATTERN_CHASE:
			/* Rotate a single bit left; wrap 0x80 → 0x01 */
			out = (uint8_t)(1u << (step % 8));
			break;

		case PATTERN_BREATHE:
			/*
			 * Fill up: 0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x7F, 0xFF
			 * Drain:   0x7F, 0x3F, 0x1F, 0x0F, 0x07, 0x03, 0x01, 0x00
			 */
			{
				uint8_t s = step % 16;
				if (s < 8) {
					out = (uint8_t)((1u << (s + 1)) - 1u);
				} else {
					out = (uint8_t)((1u << (15u - s)) - 1u);
				}
			}
			break;

		case PATTERN_BLINK:
			out = (step % 2 == 0) ? 0xFF : 0x00;
			break;

		case PATTERN_STRIPES:
			out = (step % 2 == 0) ? 0xAA : 0x55;
			break;

		default:
			out = 0;
			break;
		}

		led_write(out);
		step++;
		k_msleep(step_ms);
	}
}

/* ── Button monitor thread ──────────────────────────────────────────────── */
/*
 * Prints a log message each time a button is pressed.
 * The actual pattern advance happens in the ISR (button_pressed).
 */
static void button_monitor_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("Button monitor thread started");

	while (true) {
		/* Wait indefinitely for a button event */
		k_sem_take(&pattern_sem, K_FOREVER);
		LOG_INF("Button pressed — pattern: %s (0x%02X)",
			pattern_names[current_pattern], led_read());
		/* Small debounce delay */
		k_msleep(50);
		/* Drain any extra events accumulated during debounce */
		while (k_sem_take(&pattern_sem, K_NO_WAIT) == 0) {
		}
	}
}

/* Stack definitions */
K_THREAD_STACK_DEFINE(led_stack,   2048);
K_THREAD_STACK_DEFINE(btn_stack,   1024);
static struct k_thread led_thread_data;
static struct k_thread btn_thread_data;

/* ── main() ──────────────────────────────────────────────────────────────── */
int main(void)
{
	int ret;

	printk("Phase 8 — Zephyr RTOS LED Demo\n");
	printk("Board: %s\n", CONFIG_BOARD);
	printk("FPGA LED PIO: 0x%08X\n", LED_PIO_ADDR);

	/* ── GPIO button setup ────────────────────────────────────────────── */
	if (!gpio_is_ready_dt(&btn0)) {
		LOG_ERR("KEY0 GPIO device not ready");
		return -ENODEV;
	}
	if (!gpio_is_ready_dt(&btn1)) {
		LOG_ERR("KEY1 GPIO device not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&btn0, GPIO_INPUT);
	if (ret != 0) {
		LOG_ERR("Failed to configure KEY0: %d", ret);
		return ret;
	}
	ret = gpio_pin_configure_dt(&btn1, GPIO_INPUT);
	if (ret != 0) {
		LOG_ERR("Failed to configure KEY1: %d", ret);
		return ret;
	}

	/* Configure edge interrupts on falling edge (active-low buttons) */
	ret = gpio_pin_interrupt_configure_dt(&btn0, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret != 0) {
		LOG_ERR("Failed to configure KEY0 interrupt: %d", ret);
		return ret;
	}
	ret = gpio_pin_interrupt_configure_dt(&btn1, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret != 0) {
		LOG_ERR("Failed to configure KEY1 interrupt: %d", ret);
		return ret;
	}

	gpio_init_callback(&btn0_cb_data, button_pressed, BIT(btn0.pin));
	gpio_init_callback(&btn1_cb_data, button_pressed, BIT(btn1.pin));
	gpio_add_callback(btn0.port, &btn0_cb_data);
	gpio_add_callback(btn1.port, &btn1_cb_data);

	LOG_INF("KEY0 and KEY1 interrupts configured");

	/* ── Start threads ────────────────────────────────────────────────── */
	k_thread_create(&led_thread_data, led_stack,
			K_THREAD_STACK_SIZEOF(led_stack),
			led_pattern_thread, NULL, NULL, NULL,
			5, 0, K_NO_WAIT);
	k_thread_name_set(&led_thread_data, "led_pattern");

	k_thread_create(&btn_thread_data, btn_stack,
			K_THREAD_STACK_SIZEOF(btn_stack),
			button_monitor_thread, NULL, NULL, NULL,
			4, 0, K_NO_WAIT);
	k_thread_name_set(&btn_thread_data, "btn_monitor");

	printk("Threads started. Press KEY0 or KEY1 to change pattern.\n\n");

	/* main() returns — Zephyr idle thread takes over */
	return 0;
}
