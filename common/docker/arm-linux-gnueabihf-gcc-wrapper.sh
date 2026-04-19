#!/usr/bin/env bash
# arm-linux-gnueabihf-gcc-wrapper.sh — Run arm-linux-gnueabihf-gcc from the
# cvsoc/quartus:23.1 Docker image as a transparent compiler subprocess for
# VS Code's C/C++ IntelliSense (ms-vscode.cpptools).
#
# VS Code probes the compiler by running it with -v / -E flags to discover
# system include paths and built-in defines.  This wrapper forwards all
# arguments unchanged into the container so the probe works transparently.
#
# The workspace is mounted at its exact host path so every file path that
# VS Code passes to the compiler resolves identically inside the container.
#
# Usage (set in .vscode/c_cpp_properties.json):
#   "compilerPath": "${workspaceFolder}/common/docker/arm-linux-gnueabihf-gcc-wrapper.sh"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
DOCKER_IMAGE="cvsoc/quartus:23.1"

exec docker run --rm -i \
  -v "${REPO_ROOT}:${REPO_ROOT}" \
  "${DOCKER_IMAGE}" \
  arm-linux-gnueabihf-gcc "$@"
