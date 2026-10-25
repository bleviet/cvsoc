# Makefile for camd, the cvsoc GigE Vision camera daemon (18_gige_camera).
#
#   make                                  — host build into build/host/
#   make CC=arm-linux-gnueabihf-gcc OUT=build/arm LDFLAGS=-static
#                                         — static ARM build for the DE10-Nano

CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -std=c99
LDFLAGS ?=
OUT     ?= build/host

SRCS = camd.c camera.c gvcp.c gvsp.c source_fpga.c source_sim.c xml_blob.c
HDRS = camera.h gev.h gvcp.h gvsp.h source.h util.h
XML  = genicam/cvsoc_camera.xml

.PHONY: all clean

all: $(OUT)/camd

$(OUT)/camd: $(SRCS) $(HDRS) $(XML)
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -pthread -o $@ $(SRCS) $(LDFLAGS) -lm

clean:
	rm -rf build
