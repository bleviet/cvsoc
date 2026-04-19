# Tutorial — Building a Custom FPGA Development Docker Image

> **Series:** cvsoc — Stepping into advanced FPGA development on the DE10-Nano  
> **Type:** Tutorial (learning-oriented)  
> **Difficulty:** Beginner-Intermediate — you are comfortable with Docker basics and the Linux command line

---

## What you will build

By the end of this tutorial you will have:

- A clear understanding of **why the projects in this repository use Docker**
- A local Docker image named `cvsoc/quartus:23.1` that contains **every toolchain needed by all six project phases** — no extra installation steps required
- A working mental model of **how the upstream `raetro/quartus:23.1` image is structured** and how to extend it
- Verified that all **ARM and Nios II cross-compilers produce correct output** inside the new container

---

## Prerequisites

| Requirement | Details |
|---|---|
| **Docker** | Docker Engine 20+ or Docker Desktop installed and running |
| **Repository** | `git clone` of `bleviet/cvsoc` |
| **Internet access** | Required to pull `raetro/quartus:23.1` (~12 GB) on first use |
| **Disk space** | ~13 GB free for the base image plus the new layer |

Verify Docker is running:

```bash
docker version
# Expected: Client and Server both showing a version number
```

---

## Background: why a Docker container?

FPGA development tools are notoriously difficult to install. Quartus Prime Lite 23.1 alone requires:

- A Linux distribution with specific `glibc` and library versions
- Several hundred megabytes of device-specific synthesis libraries
- An OpenJDK 8 runtime (not 11, not 17 — specifically 8)
- A working i386 multiarch setup on x86-64 hosts (many Quartus libraries are 32-bit)

The [`raetro/quartus`](https://hub.docker.com/r/raetro/quartus) project solves this by shipping a Debian 9 container with Quartus pre-installed. You mount your project directory as a volume and all Quartus tools are immediately available, regardless of what Linux distribution you run on the host.

### What `raetro/quartus:23.1` contains

The upstream image is built in two layers:

1. **`raetro/quartus:base`** (Debian 9 / stretch-slim) — installs all system libraries Quartus needs, sets locale, adds Java, sets the `PATH` for Quartus and Nios II EDS.
2. **`raetro/quartus:23.1`** — runs the Quartus Lite 23.1 installer on top of the base, then discards the installer, leaving only the compiled tools.

What is **not** in the upstream image:
- `arm-linux-gnueabihf-gcc` — the ARM cross-compiler needed by phases 3, 4, 5, and 6
- `gdb-arm-none-eabi` — the ARM debugger needed by phase 5
- `openssh-client` — `scp` for the `deploy-elf` target

The upstream image deliberately stays minimal. The Makefiles in this repository previously worked around this by providing a `make setup` target that ran `apt-get install` inside a live container. This approach has two drawbacks:

- The installation only persists for the lifetime of the container session
- It requires downloading packages from a frozen Debian 9 snapshot archive every time

Our custom image solves both problems by baking the ARM toolchain in at build time.

---

## Step 1 — Pull the upstream image

If you do not already have `raetro/quartus:23.1` locally, pull it now. This is the only step that requires a large download.

```bash
docker pull raetro/quartus:23.1
```

Verify the pull succeeded:

```bash
docker images raetro/quartus
# REPOSITORY       TAG    IMAGE ID       CREATED        SIZE
# raetro/quartus   23.1   222ecc11edd9   18 months ago  12.2GB
```

> **Tip:** If you are on a slow connection, this download can take 20–30 minutes. Run it before following the next steps.

---

## Step 2 — Understand the Dockerfile

Open `common/docker/Dockerfile` in the repository:

```dockerfile
FROM raetro/quartus:23.1

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
        openssh-client                                                     && \
    rm -rf /var/lib/apt/lists/*

RUN arm-linux-gnueabihf-gcc  --version | head -1 && \
    arm-linux-gnueabihf-size --version | head -1 && \
    arm-none-eabi-gdb         --version | head -1
```

Each decision in this file has a reason:

### `FROM raetro/quartus:23.1`

We extend the upstream image rather than rebuilding Quartus from scratch. The Quartus installer is a 5 GB binary that requires accepting a commercial EULA and configuring device libraries. Extending the already-built image takes seconds instead of hours.

### Pinning the Debian snapshot date

The base image runs Debian 9 (stretch), which reached end-of-life in 2022. The `main` mirror no longer carries stretch packages. The Debian [snapshot.debian.org](https://snapshot.debian.org) service archives every package state ever published. Pinning to `20220622T000000Z` — the last date before the archives were cleaned up — guarantees reproducible installs.

The `Acquire::Check-Valid-Until=false` flag is required because the snapshot's `Release` file has an expiry date in the past. Without this flag, `apt-get` refuses to use the archive.

### Package list

| Package | Why it is needed |
|---|---|
| `gcc-arm-linux-gnueabihf` | Cross-compiler for ARM Cortex-A9 (HPS). Used by phases 3, 4, 5 (`arm-linux-gnueabihf-gcc`). |
| `binutils-arm-linux-gnueabihf` | ARM assembler, linker, `objcopy`, and `size`. The app Makefiles call `arm-linux-gnueabihf-objcopy` directly. |
| `libc6-dev-armhf-cross` | ARM C library headers. Required by phase 6 (`fpga_led.c` includes `<stdio.h>`, `<stdlib.h>`). |
| `linux-libc-dev-armhf-cross` | ARM kernel UAPI headers (`<asm/errno.h>`, `<linux/uio.h>`). Required by phase 6 which uses UIO. |
| `gdb-arm-none-eabi` | ARM bare-metal debugger. Used by phase 5 (`arm-none-eabi-gdb` connects to OpenOCD). |
| `openssh-client` | Provides `scp`. Used by `deploy-elf` targets across HPS phases. |

### The smoke-test `RUN`

The final `RUN` step verifies that all three key binaries are on `PATH` and executable. If any package failed to install, the Docker build fails here with a clear error — not silently at runtime hours later.

---

## Step 3 — Build the custom image

From the repository root:

```bash
docker build -t cvsoc/quartus:23.1 common/docker/
```

The build should take 1–3 minutes. Most of the time is spent downloading ~30 MB of packages from the Debian snapshot archive. You will see output like:

```
Step 1/5 : FROM raetro/quartus:23.1
 ---> 222ecc11edd9
Step 2/5 : ARG DEBIAN_SNAPSHOT="20220622T000000Z"
Step 3/5 : RUN echo "deb ..."
  ...
  Get:1 ... openssh-client ... [780 kB]
  Get:2 ... gcc-arm-linux-gnueabihf ... [6,108 kB]
  ...
  Setting up gcc-arm-linux-gnueabihf (4:6.3.0-4) ...
Step 4/5 : RUN arm-linux-gnueabihf-gcc --version ...
arm-linux-gnueabihf-gcc (Debian 6.3.0-18) 6.3.0 20170516
GNU size (GNU Binutils for Debian) 2.28
GNU gdb (7.12-6+9+b2) 7.12.0.20161007-git
Step 5/5 : LABEL ...
Successfully built df2d39b0acd8
Successfully tagged cvsoc/quartus:23.1
```

Verify the result:

```bash
docker images cvsoc/quartus
# REPOSITORY      TAG    IMAGE ID       CREATED          SIZE
# cvsoc/quartus   23.1   df2d39b0acd8   X minutes ago    12.3GB
```

The image is ~120 MB larger than the upstream because it adds the ARM cross-compiler and headers.

---

## Step 4 — Verify all tools

Run a quick verification to confirm every toolchain is accessible:

```bash
docker run --rm cvsoc/quartus:23.1 bash -c "
  echo '=== Quartus Prime 23.1 ==='
  quartus_sh --version | head -2

  echo '=== Nios II EDS (soft-core compiler) ==='
  nios2-elf-gcc --version | head -1

  echo '=== ARM cross-compiler (HPS bare-metal) ==='
  arm-linux-gnueabihf-gcc --version | head -1

  echo '=== ARM GDB (HPS debug) ==='
  arm-none-eabi-gdb --version | head -1

  echo '=== OpenOCD (JTAG debug server) ==='
  /opt/intelFPGA/quartus/bin/openocd --version 2>&1 | head -1

  echo '=== Python 3 (patch scripts) ==='
  python3 --version

  echo 'All tools OK.'
"
```

Expected output:

```
=== Quartus Prime 23.1 ===
Quartus Prime Shell
Version 23.1std.0 Build 991 11/28/2023 SC Lite Edition
=== Nios II EDS (soft-core compiler) ===
nios2-elf-gcc (Altera 23.1 Build 991) 12.3.1 20230918
=== ARM cross-compiler (HPS bare-metal) ===
arm-linux-gnueabihf-gcc (Debian 6.3.0-18) 6.3.0 20170516
=== ARM GDB (HPS debug) ===
GNU gdb (7.12-6+9+b2) 7.12.0.20161007-git
=== OpenOCD (JTAG debug server) ===
Open On-Chip Debugger 0.11.0-R22.4
=== Python 3 (patch scripts) ===
Python 3.5.3
All tools OK.
```

---

## Step 5 — Compile a project

Let's compile the HPS bare-metal LED application (phase 3) to confirm the ARM cross-compiler produces a working binary:

```bash
cd /path/to/cvsoc

docker run --rm \
  -v $(pwd):/work \
  cvsoc/quartus:23.1 \
  bash -c "cd /work/05_hps_led/software/app && make clean && make CC=arm-linux-gnueabihf-gcc"
```

Expected output:

```
rm -f hps_led.elf hps_led.bin
arm-linux-gnueabihf-gcc -mcpu=cortex-a9 -mfpu=neon-vfpv4 -mfloat-abi=hard \
  -ffreestanding -nostdlib -Os -g -Wall -Wextra \
  -T linker.ld -nostdlib -Wl,--build-id=none startup.S main.c -o hps_led.elf
arm-linux-gnueabihf-objcopy -O binary hps_led.elf hps_led.bin
arm-linux-gnueabihf-size hps_led.elf
   text    data     bss     dec     hex filename
    416     148       0     564     234 hps_led.elf
```

The output files `hps_led.elf` and `hps_led.bin` appear in `05_hps_led/software/app/` on your host — Docker writes them through the volume mount.

### What just happened

The `docker run` command:

| Flag | Effect |
|---|---|
| `--rm` | Removes the container when it exits; keeps your system clean |
| `-v $(pwd):/work` | Mounts the repository root at `/work` inside the container |
| `cvsoc/quartus:23.1` | Specifies the image |
| `bash -c "..."` | Runs a shell command; `make` runs inside the container, output files land on your host |

---

## Step 6 — The full build invocation

The project Makefiles are designed to be invoked from inside the container. The recommended pattern (shown in every `quartus/Makefile` header) is:

```bash
# From the repository root
docker run --rm -v $(pwd):/work cvsoc/quartus:23.1 \
  bash -c "cd /work/<project>/quartus && make all"
```

For example, to run the complete Nios II LED build (FPGA synthesis + BSP + app):

```bash
docker run --rm -v $(pwd):/work cvsoc/quartus:23.1 \
  bash -c "cd /work/04_nios2_led/quartus && make all"
```

Or to compile only the ARM application for the HPS interrupt project:

```bash
docker run --rm -v $(pwd):/work cvsoc/quartus:23.1 \
  bash -c "cd /work/07_hps_interrupts/quartus && make app"
```

---

## Toolchain map by project phase

| Phase | Directory | Compiler(s) used | Provided by |
|---|---|---|---|
| 0 — LED blink | `00_led_blinking` | Quartus synthesis only | `cvsoc/quartus:23.1` (Quartus from base layer) |
| 1 — LED runner | `01_led_running` | Quartus synthesis only | `cvsoc/quartus:23.1` (Quartus from base layer) |
| 2 — Nios II LED | `04_nios2_led` | `nios2-elf-gcc` | `cvsoc/quartus:23.1` (Nios II EDS from base layer) |
| 3 — HPS bare-metal | `05_hps_led` | `arm-linux-gnueabihf-gcc` | `cvsoc/quartus:23.1` (ARM toolchain layer) |
| 4 — Nios II interrupts | `06_nios2_interrupts` | `nios2-elf-gcc` | `cvsoc/quartus:23.1` (Nios II EDS from base layer) |
| 4 — HPS interrupts | `07_hps_interrupts` | `arm-linux-gnueabihf-gcc` | `cvsoc/quartus:23.1` (ARM toolchain layer) |
| 5 — Nios II debug | `08_nios2_debug` | `nios2-elf-gcc`, `nios2-elf-gdb` | `cvsoc/quartus:23.1` (Nios II EDS from base layer) |
| 5 — HPS debug | `09_hps_debug` | `arm-linux-gnueabihf-gcc`, `arm-none-eabi-gdb` | `cvsoc/quartus:23.1` (ARM toolchain layer) |
| 6 — Embedded Linux | `10_linux_led` | `arm-linux-gnueabihf-gcc` (app-cross), Buildroot on host | `cvsoc/quartus:23.1` (ARM toolchain layer) |

---

## Extending the image further

If you need additional tools, add another `RUN` layer to `common/docker/Dockerfile`:

```dockerfile
FROM raetro/quartus:23.1

# ... existing ARM toolchain layer ...

# Add your custom tools
ARG DEBIAN_SNAPSHOT="20220622T000000Z"
RUN echo "deb http://snapshot.debian.org/archive/debian/${DEBIAN_SNAPSHOT} stretch main" \
        > /etc/apt/sources.list                                && \
    apt-get update -o Acquire::Check-Valid-Until=false -qq     && \
    apt-get install -y -o Acquire::Check-Valid-Until=false \
        your-package-here                                      && \
    rm -rf /var/lib/apt/lists/*
```

Rebuild with the same `docker build` command. Because Docker layer caching is content-addressed, only the changed and subsequent layers are rebuilt.

---

## Troubleshooting

### `docker: Cannot connect to the Docker daemon`

Docker is not running. Start Docker Desktop or run `sudo systemctl start docker` on Linux.

### `manifest unknown` when pulling `raetro/quartus:23.1`

The image name might have changed upstream. Check [hub.docker.com/r/raetro/quartus](https://hub.docker.com/r/raetro/quartus) for the current tag list.

### `apt-get` fails with `Certificate verification failed`

The Debian snapshot server occasionally has slow response times. Re-run `docker build`; the layer cache means it resumes from the failed step.

### Compilation error: `arm-linux-gnueabihf-gcc: command not found`

You are running inside `raetro/quartus:23.1` instead of `cvsoc/quartus:23.1`. Verify with:

```bash
docker run --rm cvsoc/quartus:23.1 which arm-linux-gnueabihf-gcc
# Expected: /usr/bin/arm-linux-gnueabihf-gcc
```

### Output files are owned by root on the host

Docker volumes are mounted as the container root user by default. Add `--user $(id -u):$(id -g)` to the `docker run` command to write output files with your host user's ownership.

---

## Summary

You have:

1. Understood the structure of `raetro/quartus:23.1` and why it lacks the ARM cross-compiler
2. Built `cvsoc/quartus:23.1` — a local variant with all toolchains pre-installed
3. Verified every tool the repository needs is available
4. Compiled an ARM bare-metal application and seen the build output on the host

The image is now ready for all six project phases. No `make setup` step is ever needed again — the container is self-contained from the moment it starts.
