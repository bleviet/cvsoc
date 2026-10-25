# Makefile for the DE10-Nano Industrial OPC UA Server (Phase 11 — 17_opcua_hps_led)
#
# When built by Buildroot, $(CC) is set to the ARM cross-compiler automatically.
# For standalone cross-compilation:
#   make CC=arm-linux-gnueabihf-gcc
# For local host simulation / unit testing (with --sim):
#   make CC=gcc

CC     ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c99

TARGET      = led_opcua_server
TARGET_SIM  = led_opcua_server_sim
SRCS        = led_opcua_server.c ua_micro.c

.PHONY: all sim clean

all: $(TARGET)

sim: $(TARGET_SIM)

$(TARGET): $(SRCS) ua_micro.h
	$(CC) $(CFLAGS) -o $@ $(SRCS)

$(TARGET_SIM): $(SRCS) ua_micro.h
	gcc $(CFLAGS) -o $@ $(SRCS)

clean:
	rm -f $(TARGET) $(TARGET_SIM)
