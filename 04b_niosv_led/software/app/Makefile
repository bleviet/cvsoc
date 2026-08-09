# Nios V application Makefile.
# Nios V uses a CMake-based build (not the Nios II make flow): niosv-app
# generates CMakeLists.txt from main.cpp and the BSP, then cmake builds the
# ELF. The full BSP + app pipeline is driven from ../../quartus/Makefile
# (targets: bsp, app), which runs niosv-bsp / niosv-app / cmake under the
# Quartus 25.1 toolchain.
#
# This Makefile simply forwards to CMake once the BSP and the niosv-app
# generated CMakeLists.txt are in place.

BSP_DIR := ../bsp
BUILD   := build
ELF     := niosv_led.elf
CMAKE_BUILD_TYPE ?= Release

.PHONY: all clean

all: $(BUILD)/$(ELF)

$(BUILD)/$(ELF): main.cpp led.hpp CMakeLists.txt
	cmake -S . -B $(BUILD) -DCMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE)
	cmake --build $(BUILD)

clean:
	rm -rf $(BUILD)
