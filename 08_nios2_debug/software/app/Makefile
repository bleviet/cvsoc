# Nios II debug application Makefile.
# The BSP must be generated and built before this Makefile is invoked.
# See ../../quartus/Makefile which handles the full build pipeline.

BSP_DIR  := ../bsp
ELF      := nios2_debug.elf
SRCS     := main.c

CC       := nios2-elf-gcc
CFLAGS   := -DALT_SINGLE_THREADED=1 -DNIOS2 \
             -I$(BSP_DIR) \
             -I$(BSP_DIR)/HAL/inc \
             -I$(BSP_DIR)/drivers/inc \
             -O0 -g3 -Wall -Wextra \
             -mno-hw-div -mno-hw-mul

LDFLAGS  := -T$(BSP_DIR)/linker.x \
             -L$(BSP_DIR) -lhal_bsp \
             -Wl,--gc-sections

.PHONY: all clean

all: $(ELF)

$(ELF): $(SRCS) $(BSP_DIR)/linker.x
	$(CC) $(CFLAGS) $(SRCS) -o $@ $(LDFLAGS)
	nios2-elf-size $@

clean:
	rm -f $(ELF)
