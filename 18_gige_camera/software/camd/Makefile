# Makefile for camd, the cvsoc GigE Vision camera daemon (18_gige_camera).
#
#   make                                  — host build into build/host/
#   make CC=arm-linux-gnueabihf-gcc OUT=build/arm LDFLAGS=-static
#                                         — static ARM build for the DE10-Nano

CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -std=c99
LDFLAGS ?=
OUT     ?= build/host

SRCS = camd.c camera.c gvcp.c gvsp.c i2c_bitbang.c mt9p001.c source_fpga.c \
       source_sim.c xml_blob.c
HDRS = camera.h gev.h gvcp.h gvsp.h i2c_bitbang.h mt9p001.h source.h util.h
XML  = genicam/cvsoc_camera.xml

.PHONY: all check clean

all: $(OUT)/camd

$(OUT)/camd: $(SRCS) $(HDRS) $(XML)
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -pthread -o $@ $(SRCS) $(LDFLAGS) -lm

# Host unit test of the MT9P001 timing model.
check: $(OUT)/test_mt9p001
	$(OUT)/test_mt9p001

$(OUT)/test_mt9p001: test_mt9p001.c mt9p001.c i2c_bitbang.c mt9p001.h i2c_bitbang.h util.h
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -o $@ test_mt9p001.c mt9p001.c i2c_bitbang.c $(LDFLAGS) -lm

clean:
	rm -rf build
