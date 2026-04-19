/*
 * fpga_led.c — User-space FPGA LED controller via UIO on DE10-Nano.
 *
 * Controls the 8 on-board LEDs through the FPGA LED PIO peripheral using
 * the Linux UIO (Userspace I/O) framework.  The LED PIO is mapped into
 * user space via mmap() on /dev/uioX.
 *
 * The LED PIO DATA register is at offset 0x00 from the UIO mapping base.
 * Writing an 8-bit value to this register directly controls the 8 LEDs.
 *
 * Usage:
 *   fpga_led                    # Default: cycle through all patterns
 *   fpga_led 0xAA               # Set a specific LED pattern (hex)
 *   fpga_led --pattern chase    # Run a named animation
 *   fpga_led --help             # Show usage
 *
 * Build (on target):
 *   gcc -O2 -o fpga_led fpga_led.c
 *
 * Build (cross-compile):
 *   arm-linux-gnueabihf-gcc -O2 -o fpga_led fpga_led.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/mman.h>
#include <errno.h>

/* LED PIO register offsets (Altera Avalon PIO) */
#define PIO_DATA_OFFSET      0x00
#define PIO_DIRECTION_OFFSET 0x04
#define PIO_IRQ_MASK_OFFSET  0x08
#define PIO_EDGE_CAP_OFFSET  0x0C

/* UIO mapping size — one page minimum, covers all 4 PIO registers */
#define UIO_MAP_SIZE         0x1000

/* Default UIO device path */
#define DEFAULT_UIO_DEV      "/dev/uio0"

/* Number of LEDs on the DE10-Nano board */
#define NUM_LEDS             8

static volatile int running = 1;

static void signal_handler(int sig)
{
    (void)sig;
    running = 0;
}

static void usage(const char *progname)
{
    printf("Usage: %s [OPTIONS] [PATTERN]\n\n", progname);
    printf("Control FPGA LEDs on the DE10-Nano via UIO.\n\n");
    printf("Arguments:\n");
    printf("  PATTERN            Hex value (e.g. 0xAA) to set on LEDs\n\n");
    printf("Options:\n");
    printf("  -d, --device DEV   UIO device path (default: %s)\n", DEFAULT_UIO_DEV);
    printf("  -p, --pattern NAME Run a named animation:\n");
    printf("                       chase    - running light\n");
    printf("                       breathe  - fill and drain\n");
    printf("                       blink    - all LEDs blink\n");
    printf("                       stripes  - alternating pattern\n");
    printf("                       all      - cycle through all patterns\n");
    printf("  -s, --speed MS     Animation speed in milliseconds (default: 100)\n");
    printf("  -h, --help         Show this help message\n\n");
    printf("Examples:\n");
    printf("  %s 0xFF            # All LEDs on\n", progname);
    printf("  %s --pattern chase # Running light animation\n", progname);
    printf("  %s                 # Default: cycle all patterns\n", progname);
}

/* Write a value to the LED PIO DATA register */
static inline void led_write(volatile uint32_t *base, uint8_t value)
{
    *(base + (PIO_DATA_OFFSET / sizeof(uint32_t))) = value;
}

/* Read the current LED PIO DATA register value */
static inline uint8_t led_read(volatile uint32_t *base)
{
    return (uint8_t)(*(base + (PIO_DATA_OFFSET / sizeof(uint32_t))));
}

static void pattern_chase(volatile uint32_t *base, int speed_ms)
{
    uint8_t val = 0x01;
    while (running) {
        led_write(base, val);
        usleep(speed_ms * 1000);
        val <<= 1;
        if (val == 0)
            val = 0x01;
    }
}

static void pattern_breathe(volatile uint32_t *base, int speed_ms)
{
    while (running) {
        /* Fill up */
        for (int i = 0; i < NUM_LEDS && running; i++) {
            led_write(base, (1 << (i + 1)) - 1);
            usleep(speed_ms * 1000);
        }
        /* Drain */
        for (int i = NUM_LEDS - 1; i >= 0 && running; i--) {
            led_write(base, (1 << i) - 1);
            usleep(speed_ms * 1000);
        }
    }
}

static void pattern_blink(volatile uint32_t *base, int speed_ms)
{
    while (running) {
        led_write(base, 0xFF);
        usleep(speed_ms * 1000);
        if (!running) break;
        led_write(base, 0x00);
        usleep(speed_ms * 1000);
    }
}

static void pattern_stripes(volatile uint32_t *base, int speed_ms)
{
    while (running) {
        led_write(base, 0xAA);
        usleep(speed_ms * 1000);
        if (!running) break;
        led_write(base, 0x55);
        usleep(speed_ms * 1000);
    }
}

static void pattern_all(volatile uint32_t *base, int speed_ms)
{
    /* Predefined pattern sequence */
    static const uint8_t patterns[] = {
        /* Chase right */
        0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80,
        /* Chase left */
        0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01,
        /* Fill up and drain */
        0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x7F, 0xFF,
        0x7F, 0x3F, 0x1F, 0x0F, 0x07, 0x03, 0x01, 0x00,
        /* Alternating stripes */
        0xAA, 0x55, 0xAA, 0x55,
        /* All on / all off */
        0xFF, 0x00, 0xFF, 0x00,
    };
    const int num = sizeof(patterns) / sizeof(patterns[0]);
    int idx = 0;

    while (running) {
        led_write(base, patterns[idx]);
        usleep(speed_ms * 1000);
        idx = (idx + 1) % num;
    }
}

int main(int argc, char *argv[])
{
    const char *uio_dev = DEFAULT_UIO_DEV;
    const char *pattern_name = NULL;
    uint8_t static_value = 0;
    int set_static = 0;
    int speed_ms = 100;

    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else if ((strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--device") == 0) && i + 1 < argc) {
            uio_dev = argv[++i];
        } else if ((strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--pattern") == 0) && i + 1 < argc) {
            pattern_name = argv[++i];
        } else if ((strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--speed") == 0) && i + 1 < argc) {
            speed_ms = atoi(argv[++i]);
            if (speed_ms <= 0) speed_ms = 100;
        } else {
            /* Positional argument: hex LED pattern value */
            static_value = (uint8_t)strtoul(argv[i], NULL, 0);
            set_static = 1;
        }
    }

    /* Open the UIO device */
    int fd = open(uio_dev, O_RDWR | O_SYNC);
    if (fd < 0) {
        fprintf(stderr, "Error: cannot open %s: %s\n", uio_dev, strerror(errno));
        fprintf(stderr, "Hint: ensure the device tree overlay is applied and "
                        "the generic-uio driver is loaded.\n");
        fprintf(stderr, "  modprobe uio_pdrv_genirq\n");
        return 1;
    }

    /* Map the UIO device registers into user space */
    volatile uint32_t *base = (volatile uint32_t *)mmap(
        NULL, UIO_MAP_SIZE,
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        fd, 0
    );

    if (base == MAP_FAILED) {
        fprintf(stderr, "Error: mmap failed: %s\n", strerror(errno));
        close(fd);
        return 1;
    }

    printf("FPGA LED controller — UIO device: %s\n", uio_dev);
    printf("Mapped %d bytes at virtual address %p\n", UIO_MAP_SIZE, (void *)base);
    printf("Current LED value: 0x%02X\n", led_read(base));

    /* Install signal handlers for clean shutdown */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    if (set_static) {
        /* Static mode: set a single value and exit */
        led_write(base, static_value);
        printf("LEDs set to 0x%02X\n", static_value);
    } else {
        /* Animation mode */
        if (pattern_name == NULL)
            pattern_name = "all";

        printf("Running pattern: %s (speed: %d ms, Ctrl+C to stop)\n",
               pattern_name, speed_ms);

        if (strcmp(pattern_name, "chase") == 0)
            pattern_chase(base, speed_ms);
        else if (strcmp(pattern_name, "breathe") == 0)
            pattern_breathe(base, speed_ms);
        else if (strcmp(pattern_name, "blink") == 0)
            pattern_blink(base, speed_ms);
        else if (strcmp(pattern_name, "stripes") == 0)
            pattern_stripes(base, speed_ms);
        else if (strcmp(pattern_name, "all") == 0)
            pattern_all(base, speed_ms);
        else {
            fprintf(stderr, "Unknown pattern: %s\n", pattern_name);
            led_write(base, 0x00);
            munmap((void *)base, UIO_MAP_SIZE);
            close(fd);
            return 1;
        }

        /* Turn off LEDs on exit */
        led_write(base, 0x00);
        printf("\nLEDs turned off. Goodbye.\n");
    }

    munmap((void *)base, UIO_MAP_SIZE);
    close(fd);
    return 0;
}
