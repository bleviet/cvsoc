# Makefile for the M0 GigE throughput spike (18_gige_camera).
#
#   make                                  — host build into build/host/
#   make CC=arm-linux-gnueabihf-gcc OUT=build/arm LDFLAGS=-static
#                                         — static ARM build for the DE10-Nano
#
# The ARM build is static so it runs on any of the Buildroot images
# (10/11/17) regardless of their glibc version.

CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -std=c99
LDFLAGS ?=
OUT     ?= build/host

TARGETS = $(OUT)/gvsp_blast $(OUT)/gvsp_sink $(OUT)/eth_pause

.PHONY: all clean

all: $(TARGETS)

$(OUT)/%: %.c spike_common.h
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -rf build
