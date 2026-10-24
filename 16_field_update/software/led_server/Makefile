# Makefile for 16_field_update LED UDP server variants (v1, v2, crash).
#
# When invoked by Buildroot, $(CC) builds the default v1.0.0 binary (`led_server`).
# Target `variants` builds v1.0.0, v2.0.0, and the crashing test binary.

CC     ?= gcc
CFLAGS ?= -O2 -Wall -Wextra

.PHONY: all variants clean

all: led_server

variants: led_server_v1 led_server_v2 led_server_crash

led_server: led_server.c
	$(CC) $(CFLAGS) -DAPP_VERSION_STR=\"1.0.0\" -DDEFAULT_STARTUP_PATTERN=0x0F -o $@ $<

led_server_v1: led_server.c
	$(CC) $(CFLAGS) -DAPP_VERSION_STR=\"1.0.0\" -DDEFAULT_STARTUP_PATTERN=0x0F -o $@ $<

led_server_v2: led_server.c
	$(CC) $(CFLAGS) -DAPP_VERSION_STR=\"2.0.0\" -DDEFAULT_STARTUP_PATTERN=0xA5 -o $@ $<

led_server_crash: led_server.c
	$(CC) $(CFLAGS) -DAPP_VERSION_STR=\"2.0.0-broken\" -DSIMULATE_CRASH_ON_START=1 -o $@ $<

clean:
	rm -f led_server led_server_v1 led_server_v2 led_server_crash
