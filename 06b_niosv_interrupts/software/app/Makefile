# Nios V application Makefile.
# Nios V uses a CMake-based build (not the Nios II make flow): niosv-app
# generates CMakeLists.txt from main.c and the BSP, then cmake builds the
# ELF. The full BSP + app pipeline is driven from ../../quartus/Makefile
# (targets: bsp, app), which runs niosv-bsp / niosv-app / cmake under the
# Quartus 25.1 toolchain.
#
# This Makefile simply forwards to CMake once the BSP and the niosv-app
# generated CMakeLists.txt are in place.

BSP_DIR := ../bsp
BUILD   := build
ELF     := niosv_interrupts.elf

.PHONY: all clean

all: $(BUILD)/$(ELF)

$(BUILD)/$(ELF): main.c CMakeLists.txt
	cmake -S . -B $(BUILD)
	cmake --build $(BUILD)

clean:
	rm -rf $(BUILD)
