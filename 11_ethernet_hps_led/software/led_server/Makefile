# Cross-compilation Makefile for the LED UDP server.
#
# When built by Buildroot, $(CC) is set to the cross-compiler automatically.
# For standalone cross-compilation:
#   make CC=arm-linux-gnueabihf-gcc
#
# Phase 7.5 — protobuf variant:
#   make CC=arm-linux-gnueabihf-gcc pb

CC       ?= gcc
CFLAGS   ?= -O2 -Wall -Wextra

# nanopb C runtime is bundled in the nanopb/ subdirectory.
NANOPB_DIR := $(CURDIR)/nanopb

TARGET    = led_server
TARGET_PB = led_server_pb

.PHONY: all pb clean

all: $(TARGET)

pb: $(TARGET_PB)

$(TARGET): led_server.c
	$(CC) $(CFLAGS) -o $@ $<

$(TARGET_PB): led_server_pb.c led_command.pb.c
	$(CC) $(CFLAGS) -I$(NANOPB_DIR) -o $@ $< led_command.pb.c \
	    $(NANOPB_DIR)/pb_encode.c \
	    $(NANOPB_DIR)/pb_decode.c \
	    $(NANOPB_DIR)/pb_common.c

clean:
	rm -f $(TARGET) $(TARGET_PB)
