# CMakeLists.txt — Phase 8: Zephyr RTOS LED demo on DE10-Nano HPS
#
# Entry point for the Zephyr build system.
# Build with: west build -b cyclonev_socdk app/
#             (run from the zephyrproject/ workspace root)

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(zephyr_led VERSION 1.0.0)

target_sources(app PRIVATE src/main.c)
