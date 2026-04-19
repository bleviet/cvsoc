#!/usr/bin/env bash
# nios2-elf-gdb-wrapper.sh — Run nios2-elf-gdb from the cvsoc/quartus:23.1
# Docker image as a transparent GDB MI subprocess for VS Code's cppdbg extension.
#
# The workspace is mounted at its exact host path so every path that VS Code
# passes to GDB (ELF files, source files) resolves identically inside the
# container without any translation.
#
# --network host lets the GDB client reach the nios2-gdb-server running on
# localhost:2345 (started by 'make gdb-server' in 08_nios2_debug/quartus/).
#
# Usage (set in .vscode/launch.json):
#   "miDebuggerPath": "${workspaceFolder}/common/docker/nios2-elf-gdb-wrapper.sh"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
DOCKER_IMAGE="cvsoc/quartus:23.1"

exec docker run --rm -i \
  --network host \
  -v "${REPO_ROOT}:${REPO_ROOT}" \
  "${DOCKER_IMAGE}" \
  nios2-elf-gdb "$@"
