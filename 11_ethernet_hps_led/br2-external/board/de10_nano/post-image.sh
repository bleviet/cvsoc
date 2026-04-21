#!/bin/bash
# post-image.sh — Runs after Buildroot generates filesystem images.
#
# Patches the boot DTB (same as Phase 6) and assembles the boot partition.
# See Phase 6 (10_linux_led) post-image.sh for detailed commentary.

set -e

IMAGES_DIR="${BINARIES_DIR}"
BOARD_DIR="$(dirname "$0")"
EXTERNAL_DIR="$(dirname "$(dirname "$BOARD_DIR")")"
PROJECT_DIR="$(dirname "$EXTERNAL_DIR")"

FDTPUT="${HOST_DIR}/bin/fdtput"
FDTGET="${HOST_DIR}/bin/fdtget"
MKIMAGE="${HOST_DIR}/bin/mkimage"
if [ ! -x "$MKIMAGE" ]; then
    MKIMAGE=$(find "${BUILD_DIR}/uboot-"*/tools/mkimage 2>/dev/null | head -1)
fi

BASE_DTB="${IMAGES_DIR}/socfpga_cyclone5_de0_nano_soc.dtb"

# ── Patch the DTB (identical to Phase 6) ─────────────────────────────────────
if [ -f "$BASE_DTB" ] && [ -x "$FDTPUT" ]; then
    echo "post-image.sh: patching DTB for FPGA region + LW bridge..."

    "$FDTPUT" -t x "$BASE_DTB" /soc/fpga_bridge@ff400000 phandle 0x40
    "$FDTPUT" -t s "$BASE_DTB" /soc/fpga_bridge@ff400000 status okay
    "$FDTPUT"    "$BASE_DTB" /soc/base_fpga_region ranges
    "$FDTPUT" -t s "$BASE_DTB" /soc/base_fpga_region firmware-name de10_nano.rbf
    "$FDTPUT" -t x "$BASE_DTB" /soc/base_fpga_region fpga-bridges 0x40

    DTC="${HOST_DIR}/bin/dtc"
    TMP_DTS="${BASE_DTB%.dtb}_patched.dts"
    "$DTC" -I dtb -O dts -o "$TMP_DTS" "$BASE_DTB" 2>/dev/null
    python3 - "$TMP_DTS" <<'PYEOF'
import sys, re

path = sys.argv[1]
with open(path) as f:
    content = f.read()

uio_node = (
    "\t\t\tled-controller@ff200000 {\n"
    "\t\t\t\tcompatible = \"generic-uio\";\n"
    "\t\t\t\treg = <0xff200000 0x10>;\n"
    "\t\t\t\tstatus = \"okay\";\n"
    "\t\t\t};\n"
)

old = "\t\t\t#size-cells = <0x01>;\n\t\t};"
new = "\t\t\t#size-cells = <0x01>;\n\n" + uio_node + "\t\t};"
content = content.replace(old, new, 1)

with open(path, 'w') as f:
    f.write(content)
PYEOF
    "$DTC" -I dts -O dtb -o "$BASE_DTB" "$TMP_DTS" 2>/dev/null
    rm -f "$TMP_DTS"

    echo "post-image.sh: DTB patched"
else
    echo "post-image.sh: WARNING — DTB or fdtput not found, skipping DTB patch"
fi

# ── Copy the FPGA bitstream ───────────────────────────────────────────────────
RBF_FILE="${PROJECT_DIR}/de10_nano.rbf"
if [ ! -f "$RBF_FILE" ]; then
    RBF_FILE="${PROJECT_DIR}/../10_linux_led/de10_nano.rbf"
fi
if [ -f "$RBF_FILE" ]; then
    cp "$RBF_FILE" "${IMAGES_DIR}/de10_nano.rbf"
    echo "post-image.sh: copied FPGA bitstream to ${IMAGES_DIR}"
else
    echo "post-image.sh: WARNING — de10_nano.rbf not found"
fi

# ── extlinux.conf ─────────────────────────────────────────────────────────────
mkdir -p "${IMAGES_DIR}/extlinux"
cp "${BOARD_DIR}/extlinux.conf" "${IMAGES_DIR}/extlinux/extlinux.conf"
echo "post-image.sh: extlinux.conf installed"

# ── boot.scr for U-Boot auto-boot ────────────────────────────────────────────
if [ -x "$MKIMAGE" ]; then
    BOOT_SCR_SRC="${IMAGES_DIR}/boot.txt"
    cat > "$BOOT_SCR_SRC" <<'BOOTSCRIPT'
# DE10-Nano cvsoc Phase 7 boot script
echo "=== cvsoc Phase 7 — Ethernet LED control ==="
setenv bootargs "root=/dev/mmcblk0p3 rootwait console=ttyS0,115200n8 uio_pdrv_genirq.of_id=generic-uio"
load mmc 0:2 ${kernel_addr_r} zImage
load mmc 0:2 ${fdt_addr_r} socfpga_cyclone5_de0_nano_soc.dtb
bootz ${kernel_addr_r} - ${fdt_addr_r}
BOOTSCRIPT
    "$MKIMAGE" -A arm -O linux -T script -C none \
        -n "cvsoc Phase 7 boot script" \
        -d "$BOOT_SCR_SRC" "${IMAGES_DIR}/boot.scr" 2>/dev/null
    rm -f "$BOOT_SCR_SRC"
    echo "post-image.sh: boot.scr created"
fi

echo "post-image.sh: boot artifacts prepared in ${IMAGES_DIR}"
