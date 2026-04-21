# Phase 8 Dependency Installation Guide

> **OS:** Ubuntu/Debian (WSL2 or native Linux)  
> **Zephyr SDK version:** 1.0.1 (latest as of April 2026)  
> **Zephyr version:** v3.7.0 (LTS, used in `12_zephyr_led/west.yml`)

Run the commands below **in order** before running `make init` in `12_zephyr_led/`.

---

## Step 1 — System packages

```bash
sudo apt-get update
sudo apt-get install -y \
    git cmake ninja-build gperf \
    ccache dfu-util wget curl \
    python3-dev python3-pip python3-venv \
    python3-setuptools python3-tk python3-wheel \
    xz-utils file make gcc gcc-multilib \
    g++-multilib libsdl2-dev libmagic1
```

---

## Step 2 — West meta-tool

```bash
pip3 install --user west
# Add ~/.local/bin to PATH if not already there:
echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc
west --version   # should print: west X.Y.Z
```

---

## Step 3 — Zephyr SDK v1.0.1

> **Important (WSL2):** Install into `$HOME` (on the WSL2 ext4 partition `/dev/sdd`),  
> **not** into `/opt` (which maps to the Windows C:\ drive with limited free space).  
> Your WSL2 partition has 669 GB free — plenty of room.

```bash
# 3a. Download the GNU SDK bundle with SHA256 verification
cd ~
wget https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v1.0.1/zephyr-sdk-1.0.1_linux-x86_64_gnu.tar.xz
wget -O - https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v1.0.1/sha256.sum \
    | shasum --check --ignore-missing

# 3b. Extract (to $HOME — about 3.5 GB extracted)
tar xvf zephyr-sdk-1.0.1_linux-x86_64_gnu.tar.xz

# 3c. Run setup script (registers SDK in CMake package registry)
cd ~/zephyr-sdk-1.0.1
./setup.sh

# 3d. Install udev rules for USB-Blaster and other JTAG probes
sudo cp ~/zephyr-sdk-1.0.1/hosttools/sysroots/x86_64-pokysdk-linux/usr/share/openocd/contrib/60-openocd.rules \
    /etc/udev/rules.d/
sudo udevadm control --reload

# 3e. Clean up the archive
rm ~/zephyr-sdk-1.0.1_linux-x86_64_gnu.tar.xz
```

Verify:
```bash
ls ~/zephyr-sdk-1.0.1/arm-zephyr-eabi/bin/arm-zephyr-eabi-gcc
# Should exist
```

---

## Step 4 — Zephyr workspace Python dependencies

These are installed after `west init` populates `~/zephyrproject/` (done by `make init`).

```bash
# Run 'make init' first in 12_zephyr_led/, then:
pip3 install --user -r ~/zephyrproject/zephyr/scripts/requirements.txt
```

---

## Step 5 — USB-Blaster II udev rules (JTAG flashing)

The USB-Blaster II is the DE10-Nano's built-in JTAG interface (same one used by Quartus). Add udev rules so it works without `sudo`:

```bash
cat << 'EOF' | sudo tee /etc/udev/rules.d/51-altera-usb-blaster.rules
# Altera USB-Blaster II
SUBSYSTEM=="usb", ENV{DEVTYPE}=="usb_device", ATTR{idVendor}=="09fb", ATTR{idProduct}=="6010", MODE="0666"
SUBSYSTEM=="usb", ENV{DEVTYPE}=="usb_device", ATTR{idVendor}=="09fb", ATTR{idProduct}=="6810", MODE="0666"
EOF
sudo udevadm control --reload
```

> **WSL2 note:** USB devices must be forwarded to WSL2 via `usbipd`. The DE10-Nano USB-Blaster  
> is typically already forwarded if you used it in earlier phases with Quartus.

---

## Verification checklist

```bash
west --version              # west 1.x.y
cmake --version             # cmake 3.20+
ninja --version             # ninja 1.x
python3 --version           # 3.10+
ls ~/zephyr-sdk-1.0.1/arm-zephyr-eabi/bin/arm-zephyr-eabi-gcc
```

---

## Version compatibility reference

| Component | Version | Notes |
|-----------|---------|-------|
| Zephyr RTOS | v3.7.0 (LTS) | Pinned in `west.yml` |
| Zephyr SDK | v1.0.1 | Latest; includes `arm-zephyr-eabi` GCC |
| West | ≥ 1.2.0 | Install via pip |
| CMake | ≥ 3.20 | System package |
| Python | ≥ 3.10 | System package |
