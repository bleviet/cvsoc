# cvsoc/quartus:23.1 — Custom build image for the bleviet/cvsoc tutorial series.
#
# Extends raetro/quartus:23.1 (Quartus Prime Lite 23.1 + Nios II EDS) with every
# additional toolchain required by the projects in this repository:
#
#   arm-linux-gnueabihf-gcc   — HPS bare-metal cross-compiler (05, 07, 09, 10)
#   arm-linux-gnueabihf-*     — ARM binutils (objcopy, size, ld, …)
#   gdb-arm-none-eabi         — ARM GDB for OpenOCD debug sessions (09_hps_debug)
#   openssh-client            — scp for deploy-elf targets
#   python3                   — already present; patch scripts + timing check
#
# Build:
#   docker build -t cvsoc/quartus:23.1 common/docker/
#
# Run (from repo root):
#   docker run --rm -v $(pwd):/work cvsoc/quartus:23.1 \
#     bash -c "cd /work/<project>/quartus && make all"

FROM raetro/quartus:23.1

# The base image is Debian 9 (stretch).  The main mirrors no longer carry
# stretch packages, so we pin to the snapshot archive at a known-good date.
ARG DEBIAN_SNAPSHOT="20220622T000000Z"

RUN echo "deb http://snapshot.debian.org/archive/debian/${DEBIAN_SNAPSHOT} stretch main" \
        > /etc/apt/sources.list                                              && \
    apt-get update -o Acquire::Check-Valid-Until=false -qq                   && \
    apt-get install -y -o Acquire::Check-Valid-Until=false --no-install-recommends \
        gcc-arm-linux-gnueabihf                                               \
        binutils-arm-linux-gnueabihf                                          \
        libc6-dev-armhf-cross                                                 \
        linux-libc-dev-armhf-cross                                            \
        gdb-arm-none-eabi                                                     \
        openssh-client                                                        && \
    rm -rf /var/lib/apt/lists/*

# Smoke-test: verify every added binary is on PATH
RUN arm-linux-gnueabihf-gcc  --version | head -1 && \
    arm-linux-gnueabihf-size --version | head -1 && \
    arm-none-eabi-gdb         --version | head -1

LABEL \
    org.opencontainers.image.title="cvsoc/quartus"                               \
    org.opencontainers.image.description="Quartus 23.1 + ARM cross-compiler for bleviet/cvsoc" \
    org.opencontainers.image.source="https://github.com/bleviet/cvsoc"
