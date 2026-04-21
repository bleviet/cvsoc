# Cross-compilation Makefile for the LED UDP server.
#
# When built by Buildroot, $(CC) is set to the cross-compiler automatically.
# For standalone cross-compilation:
#   make CC=arm-linux-gnueabihf-gcc

CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra
TARGET   = led_server

.PHONY: all clean

all: $(TARGET)

$(TARGET): led_server.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TARGET)
