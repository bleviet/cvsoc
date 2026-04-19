#!/bin/bash
# post-image.sh — Runs after Buildroot generates filesystem images.
#
# Patches the boot DTB so that:
#   1. base_fpga_region gets firmware-name, fpga-bridges, and ranges
#   2. fpga_bridge@ff400000 (LW H2F) gets a phandle and status=okay
#   3. led-controller@ff200000 UIO child node is added to base_fpga_region
#
# Also copies the FPGA bitstream, extlinux config, and generates boot.scr.
#
# Buildroot sets these environment variables before calling this script:
#   BINARIES_DIR — path to output/images/
#   HOST_DIR     — path to output/host/ (contains host tools like dtc/fdtput)
#   BUILD_DIR    — path to output/build/
#   BASE_DIR     — path to output/

set -e

IMAGES_DIR="${BINARIES_DIR}"
BOARD_DIR="$(dirname "$0")"
EXTERNAL_DIR="$(dirname "$(dirname "$BOARD_DIR")")"
PROJECT_DIR="$(dirname "$EXTERNAL_DIR")"

FDTPUT="${HOST_DIR}/bin/fdtput"
FDTGET="${HOST_DIR}/bin/fdtget"
# mkimage is installed to host/bin/ during the U-Boot build
MKIMAGE="${HOST_DIR}/bin/mkimage"
if [ ! -x "$MKIMAGE" ]; then
    # Fallback: find it in the U-Boot build tree
    MKIMAGE=$(find "${BUILD_DIR}/uboot-"*/tools/mkimage 2>/dev/null | head -1)
fi

BASE_DTB="${IMAGES_DIR}/socfpga_cyclone5_de0_nano_soc.dtb"

# ── Patch the DTB with fdtput ─────────────────────────────────────────────────
if [ -f "$BASE_DTB" ] && [ -x "$FDTPUT" ]; then
    echo "post-image.sh: patching DTB for FPGA region + LW bridge..."

    # 1. Add a phandle to the LW H2F bridge so the region can reference it
    "$FDTPUT" -t x "$BASE_DTB" /soc/fpga_bridge@ff400000 phandle 0x40

    # 2. Enable the LW H2F bridge
    "$FDTPUT" -t s "$BASE_DTB" /soc/fpga_bridge@ff400000 status okay

    # 3. Add ranges (empty) to base_fpga_region so child regs translate correctly
    "$FDTPUT" "$BASE_DTB" /soc/base_fpga_region ranges

    # 4. Point base_fpga_region at the FPGA bitstream in /lib/firmware/
    "$FDTPUT" -t s "$BASE_DTB" /soc/base_fpga_region firmware-name de10_nano.rbf

    # 5. Reference the LW H2F bridge from the region
    "$FDTPUT" -t x "$BASE_DTB" /soc/base_fpga_region fpga-bridges 0x40

    # 6. Add the LED PIO UIO child node inside base_fpga_region.
    #    fdtput (DTC 1.7.x) cannot create new nodes, so we do a DTS round-trip:
    #    decompile → inject node text → recompile.
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

    echo "post-image.sh: DTB patched:"
    echo "  bridge@ff400000 phandle=$("$FDTGET" -t x "$BASE_DTB" /soc/fpga_bridge@ff400000 phandle) status=$("$FDTGET" "$BASE_DTB" /soc/fpga_bridge@ff400000 status)"
    echo "  base_fpga_region firmware-name=$("$FDTGET" "$BASE_DTB" /soc/base_fpga_region firmware-name)"
    echo "  led-controller compatible=$("$FDTGET" "$BASE_DTB" /soc/base_fpga_region/led-controller@ff200000 compatible)"
else
    echo "post-image.sh: WARNING — DTB or fdtput not found, skipping DTB patch"
fi

# ── Copy the FPGA bitstream ───────────────────────────────────────────────────
RBF_FILE="${PROJECT_DIR}/de10_nano.rbf"
if [ -f "$RBF_FILE" ]; then
    cp "$RBF_FILE" "${IMAGES_DIR}/de10_nano.rbf"
    echo "post-image.sh: copied FPGA bitstream to ${IMAGES_DIR}"
else
    echo "post-image.sh: WARNING — de10_nano.rbf not found, FPGA must be loaded manually"
fi

# ── extlinux.conf for U-Boot sysboot / distro_boot ───────────────────────────
mkdir -p "${IMAGES_DIR}/extlinux"
cp "${BOARD_DIR}/extlinux.conf" "${IMAGES_DIR}/extlinux/extlinux.conf"
echo "post-image.sh: extlinux.conf installed"

# ── boot.scr for U-Boot automatic boot (distro_bootcmd) ──────────────────────
# U-Boot's distro_bootcmd searches FAT partition for boot.scr first.
# This gives auto-boot without needing to set bootcmd manually.
if [ -x "$MKIMAGE" ]; then
    BOOT_SCR_SRC="${IMAGES_DIR}/boot.txt"
    cat > "$BOOT_SCR_SRC" <<'BOOTSCRIPT'
# DE10-Nano cvsoc Phase 6 boot script
# Loaded automatically by U-Boot distro_bootcmd from the FAT partition.
echo "=== cvsoc Phase 6 — Embedded Linux boot ==="
setenv bootargs "root=/dev/mmcblk0p3 rootwait console=ttyS0,115200n8"
load mmc 0:2 ${kernel_addr_r} zImage
load mmc 0:2 ${fdt_addr_r} socfpga_cyclone5_de0_nano_soc.dtb
bootz ${kernel_addr_r} - ${fdt_addr_r}
BOOTSCRIPT
    "$MKIMAGE" -A arm -O linux -T script -C none \
        -n "cvsoc boot script" \
        -d "$BOOT_SCR_SRC" "${IMAGES_DIR}/boot.scr" 2>/dev/null
    rm -f "$BOOT_SCR_SRC"
    echo "post-image.sh: boot.scr created for U-Boot auto-boot"
else
    echo "post-image.sh: WARNING — mkimage not found, boot.scr not generated (manual boot needed)"
fi

echo "post-image.sh: boot artifacts prepared in ${IMAGES_DIR}"
