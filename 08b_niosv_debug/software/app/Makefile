# Nios V application Makefile.
# Nios V uses a CMake-based build (not the Nios II make flow): niosv-app
# generates CMakeLists.txt from main.c and the BSP, then cmake builds the
# ELF. The full BSP + app pipeline is driven from ../../quartus/Makefile
# (target: all), which reuses the 06b_niosv_interrupts BSP, runs niosv-app,
# and then cmake under the Quartus 25.1 toolchain.
#
# This Makefile simply forwards to CMake once the BSP and the niosv-app
# generated CMakeLists.txt are in place.

BSP_DIR := ../bsp
BUILD   := build
ELF     := niosv_debug.elf

.PHONY: all clean

all: $(BUILD)/$(ELF)

$(BUILD)/$(ELF): main.c CMakeLists.txt
	cmake -S . -B $(BUILD)
	cmake --build $(BUILD)

clean:
	rm -rf $(BUILD)
