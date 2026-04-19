#!/usr/bin/env python3
"""
fpga_led.py — Control FPGA LEDs on the DE10-Nano using mmap.

Demonstrates direct register access from Python via /dev/mem.

Usage:
    fpga_led.py                       # Default: cycle LED patterns via /dev/mem
    fpga_led.py --value 0xAA          # Set a specific LED pattern
    fpga_led.py --pattern chase       # Run a named animation
    fpga_led.py --uio                 # Use /dev/uio0 instead of /dev/mem
    fpga_led.py --help                # Show usage

The LED PIO is an Altera Avalon PIO peripheral on the Lightweight HPS-to-FPGA
bridge.  Its DATA register is at physical address 0xFF200000.

Note: the FPGA must be programmed and the LW H2F bridge must be enabled before
running this script. Both are handled by the fpga_load kernel module loaded
via /etc/init.d/S30fpga_load at boot.
"""

import argparse
import mmap
import os
import signal
import struct
import sys
import time

# LED PIO physical address on the Lightweight HPS-to-FPGA bridge
LW_BRIDGE_BASE = 0xFF200000
LED_PIO_OFFSET = 0x0000
LED_PIO_ADDR   = LW_BRIDGE_BASE + LED_PIO_OFFSET

# UIO mapping: offset is always 0 (the UIO driver maps the register region)
UIO_MAP_OFFSET = 0
UIO_MAP_SIZE   = 0x1000  # minimum one page

# PIO register offsets
PIO_DATA      = 0x00
PIO_DIRECTION = 0x04
PIO_IRQ_MASK  = 0x08
PIO_EDGE_CAP  = 0x0C

running = True


def signal_handler(sig, frame):
    """Handle Ctrl+C for clean shutdown."""
    global running
    running = False


def open_uio(device="/dev/uio0"):
    """Open and mmap a UIO device. Returns (fd, mmap_object)."""
    fd = os.open(device, os.O_RDWR | os.O_SYNC)
    mem = mmap.mmap(fd, UIO_MAP_SIZE, mmap.MAP_SHARED, mmap.PROT_READ | mmap.PROT_WRITE,
                    offset=UIO_MAP_OFFSET)
    return fd, mem


def open_devmem():
    """Open /dev/mem and mmap the LED PIO region. Returns (fd, mmap_object)."""
    fd = os.open("/dev/mem", os.O_RDWR | os.O_SYNC)
    # Align to page boundary
    page_size = mmap.PAGESIZE
    page_base = LED_PIO_ADDR & ~(page_size - 1)
    mem = mmap.mmap(fd, page_size, mmap.MAP_SHARED, mmap.PROT_READ | mmap.PROT_WRITE,
                    offset=page_base)
    return fd, mem


def led_write(mem, value, offset=PIO_DATA):
    """Write a 32-bit value to a PIO register."""
    mem.seek(offset)
    mem.write(struct.pack("<I", value & 0xFF))


def led_read(mem, offset=PIO_DATA):
    """Read a 32-bit value from a PIO register."""
    mem.seek(offset)
    return struct.unpack("<I", mem.read(4))[0] & 0xFF


def pattern_chase(mem, speed):
    """Running light pattern."""
    val = 0x01
    while running:
        led_write(mem, val)
        time.sleep(speed)
        val = (val << 1) & 0xFF
        if val == 0:
            val = 0x01


def pattern_breathe(mem, speed):
    """Fill up and drain pattern."""
    while running:
        for i in range(8):
            if not running:
                return
            led_write(mem, (1 << (i + 1)) - 1)
            time.sleep(speed)
        for i in range(7, -1, -1):
            if not running:
                return
            led_write(mem, (1 << i) - 1)
            time.sleep(speed)


def pattern_blink(mem, speed):
    """All LEDs blink on/off."""
    while running:
        led_write(mem, 0xFF)
        time.sleep(speed)
        if not running:
            return
        led_write(mem, 0x00)
        time.sleep(speed)


def pattern_all(mem, speed):
    """Cycle through all predefined patterns."""
    patterns = [
        # Chase right
        0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80,
        # Chase left
        0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01,
        # Fill and drain
        0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x7F, 0xFF,
        0x7F, 0x3F, 0x1F, 0x0F, 0x07, 0x03, 0x01, 0x00,
        # Stripes
        0xAA, 0x55, 0xAA, 0x55,
        # Blink
        0xFF, 0x00, 0xFF, 0x00,
    ]
    idx = 0
    while running:
        led_write(mem, patterns[idx])
        time.sleep(speed)
        idx = (idx + 1) % len(patterns)


PATTERNS = {
    "chase": pattern_chase,
    "breathe": pattern_breathe,
    "blink": pattern_blink,
    "all": pattern_all,
}


def main():
    parser = argparse.ArgumentParser(
        description="Control FPGA LEDs on the DE10-Nano via /dev/mem or UIO.")
    parser.add_argument("--uio", action="store_true",
                        help="Use /dev/uio0 instead of /dev/mem (requires device tree overlay)")
    parser.add_argument("--device", "-d", default="/dev/uio0",
                        help="UIO device path (only used with --uio, default: /dev/uio0)")
    parser.add_argument("--value", "-v", type=lambda x: int(x, 0),
                        help="Set a static LED pattern (hex, e.g. 0xAA)")
    parser.add_argument("--pattern", "-p", default="all",
                        choices=list(PATTERNS.keys()),
                        help="Animation pattern (default: all)")
    parser.add_argument("--speed", "-s", type=float, default=0.1,
                        help="Animation speed in seconds (default: 0.1)")
    args = parser.parse_args()

    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)

    try:
        if args.uio:
            print(f"Opening {args.device}...")
            fd, mem = open_uio(args.device)
        else:
            print(f"Opening /dev/mem (LED PIO at 0x{LED_PIO_ADDR:08X})...")
            fd, mem = open_devmem()
    except OSError as e:
        print(f"Error: {e}", file=sys.stderr)
        if args.uio:
            print("Hint: ensure the device tree overlay is applied and "
                  "the UIO driver is loaded.", file=sys.stderr)
        else:
            print("Hint: ensure the FPGA is programmed and the LW H2F bridge is enabled.",
                  file=sys.stderr)
            print("  Check: dmesg | grep fpga_load", file=sys.stderr)
        sys.exit(1)

    print(f"Current LED value: 0x{led_read(mem):02X}")

    if args.value is not None:
        led_write(mem, args.value)
        print(f"LEDs set to 0x{args.value & 0xFF:02X}")
    else:
        print(f"Running pattern: {args.pattern} (speed: {args.speed}s, Ctrl+C to stop)")
        try:
            PATTERNS[args.pattern](mem, args.speed)
        except KeyboardInterrupt:
            pass
        led_write(mem, 0x00)
        print("\nLEDs turned off. Goodbye.")

    mem.close()
    os.close(fd)


if __name__ == "__main__":
    main()
