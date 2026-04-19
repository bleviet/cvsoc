#!/usr/bin/env bash
# arm-none-eabi-gdb-wrapper.sh — Run arm-none-eabi-gdb from the
# cvsoc/quartus:23.1 Docker image as a transparent GDB MI subprocess for
# VS Code's cppdbg extension.
#
# The workspace is mounted at its exact host path so every path that VS Code
# passes to GDB (ELF files, source files) resolves identically inside the
# container without any translation.
#
# --network host lets the GDB client reach the OpenOCD server running on
# localhost:3333 (started by 'make openocd' in 09_hps_debug/quartus/).
#
# Usage (set in .vscode/launch.json):
#   "miDebuggerPath": "${workspaceFolder}/common/docker/arm-none-eabi-gdb-wrapper.sh"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
DOCKER_IMAGE="cvsoc/quartus:23.1"

exec docker run --rm -i \
  --network host \
  -v "${REPO_ROOT}:${REPO_ROOT}" \
  "${DOCKER_IMAGE}" \
  arm-none-eabi-gdb "$@"
