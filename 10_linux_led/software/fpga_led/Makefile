# Cross-compilation Makefile for the FPGA LED user-space application.
#
# When built by Buildroot, $(CC) is set to the cross-compiler automatically.
# For standalone cross-compilation:
#   make CC=arm-linux-gnueabihf-gcc

CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra
TARGET   = fpga_led

.PHONY: all clean

all: $(TARGET)

$(TARGET): fpga_led.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TARGET)
