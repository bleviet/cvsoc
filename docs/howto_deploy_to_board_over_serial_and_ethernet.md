# How-to: Deploy Binaries, Kernel Modules, and Bitstreams to the DE10-Nano Over Ethernet or Serial

> **Series:** cvsoc — Stepping into advanced FPGA development on the DE10-Nano  
> **Type:** How-to Guide (Problem-oriented)  
> **Difficulty:** Intermediate  
> **Applies to:** Phase 6 (`10_linux_led`), Phase 7 (`11_ethernet_hps_led`), Phase 9 (`13_secure_boot`), Phase 10 (`16_field_update`)

---

## Overview

A full Buildroot SD card build (`make buildroot`) takes 15–30 minutes on the first run and requires physically removing the microSD card from the DE10-Nano to re-flash it with `dd`.

When you are iterating on a C user-space daemon (`fpga_led`, `led_server`), a kernel module (`fpga_load.ko`), or a compressed FPGA bitstream (`de10_nano.rbf`), you can cross-compile your change in **under 2 seconds** and deploy it directly to the running board.

This guide provides recipes for:
1. **Zero-DHCP IPv6 Link-Local Discovery & SSH Setup** (when the board is plugged directly into your PC or USB-Ethernet adapter with no router).
2. **Replacing a Running Binary Over `scp` Without Hitting `ETXTBSY` (`Text file busy`)**.
3. **Hot-Reloading a New FPGA `.rbf` Bitstream Safely from Linux**.
4. **Transferring Files Over the `/dev/ttyUSB0` Serial Console (`base64 + gzip`)** when you have no Ethernet or SSH server (such as on a stock Phase 6 image).

---

## Recipe 1: Discover the Board's IPv6 Address and Connect Over SSH (Zero DHCP)

When you connect an Ethernet cable directly between your PC and the DE10-Nano, neither side needs a DHCP server or static IPv4 configuration—Linux automatically assigns an **IPv6 Link-Local (`fe80::/64`)** address to `eth0`.

### Step 1 — Bring up the host Ethernet interface

Find your PC's Ethernet interface name (for example, `eth0` or `enx08beac224c03` for a USB-Ethernet adapter) and ensure the link is UP:

```bash
ip link show
sudo ip link set enx08beac224c03 up
```

### Step 2 — Ping the IPv6 All-Nodes Multicast Address (`ff02::1`)

```bash
ping6 -c 3 -I enx08beac224c03 ff02::1
```

You will see replies from two `fe80::` addresses: one is your host PC, and the other (marked `(DUP!)` or with a different MAC-derived suffix) is the DE10-Nano:

```text
64 bytes from fe80::a8be:acff:fe22:4c03%enx08beac224c03: icmp_seq=1 ttl=64 time=0.05 ms   <-- Host PC
64 bytes from fe80::2833:8aff:fe95:cb3d%enx08beac224c03: icmp_seq=1 ttl=64 time=0.42 ms   <-- DE10-Nano!
```

### Step 3 — Install your SSH public key for passwordless login (Phase 7+ images)

Always include the **`%<interface>` zone ID** when connecting to a `fe80::` link-local address (and wrap the address in `[...]` when using `scp`):

```bash
BOARD="fe80::2833:8aff:fe95:cb3d%enx08beac224c03"

# Copy SSH key once (password is "root"):
ssh-copy-id "root@${BOARD}"

# Verify passwordless SSH works:
ssh "root@${BOARD}" uname -a
```

---

## Recipe 2: Cross-Compile and Replace a Running Daemon Without `ETXTBSY`

### Why `scp led_server root@board:/usr/bin/led_server` Fails
If `/usr/bin/led_server` is currently executing on the DE10-Nano, the Linux kernel locks its backing inode against modification. Attempting to overwrite `/usr/bin/led_server` directly via `scp` fails with:
```text
scp: /usr/bin/led_server: Text file busy
```

### The Atomic Upload-and-Swap Recipe

1. **Cross-compile on the host** (takes ~1 second):
   ```bash
   # In 11_ethernet_hps_led/:
   make server-cross
   ```
2. **Upload to `/tmp/` (RAM disk) and atomically `mv` into `/usr/bin/`:**
   Unlike overwriting a file in-place (`open(O_WRONLY|O_TRUNC)`), **`mv` (`rename(2)`) replaces the directory entry**, which Linux allows even while the old inode is still mapped, or after stopping the service:
   ```bash
   BOARD="fe80::2833:8aff:fe95:cb3d%enx08beac224c03"

   # 1. Upload to /tmp on the board (note brackets around IPv6 address for scp):
   scp software/led_server/led_server "root@[${BOARD}]:/tmp/led_server.new"

   # 2. Stop daemon, swap binary, and restart daemon:
   ssh "root@${BOARD}" "/etc/init.d/S40led_server stop && \
                        mv /tmp/led_server.new /usr/bin/led_server && \
                        chmod +x /usr/bin/led_server && \
                        /etc/init.d/S40led_server start"
   ```

---

## Recipe 3: Safely Hot-Reload a New FPGA Bitstream (`.rbf`) on a Running Linux Board

> **WARNING:** Never reload `/lib/firmware/de10_nano.rbf` while a user-space process (`fpga_led` or `led_server`) is actively reading/writing `0xFF200000`! Always stop MMIO consumers first and let `fpga_load.ko` gate the AXI bridges in reset.

```bash
BOARD="fe80::2833:8aff:fe95:cb3d%enx08beac224c03"

# 1. Convert your updated .sof to a compressed .rbf (inside 10_linux_led/):
make rbf -C 10_linux_led

# 2. Copy the compressed .rbf to /lib/firmware/ on the board:
scp 10_linux_led/de10_nano.rbf "root@[${BOARD}]:/lib/firmware/de10_nano.rbf"

# 3. Stop MMIO apps, reload fpga_load.ko (which gates bridges, programs FPGA, and re-enables bridges), and restart app:
ssh "root@${BOARD}" " \
  /etc/init.d/S40led_server stop 2>/dev/null || true; \
  rmmod fpga_load 2>/dev/null || true; \
  modprobe fpga_load && \
  /etc/init.d/S40led_server start 2>/dev/null || true; \
  dmesg | tail -n 6"
```

Verify the `dmesg` output ends with:
```text
fpga_load: CONF_DONE! ...
fpga_load: final state=4 (need 4=USER_MODE)
fpga_load: FPGA programmed successfully!
fpga_load: LW H2F bridge enabled
```

---

## Recipe 4: Transfer a Binary Over `/dev/ttyUSB0` Serial UART (When SSH Is Unavailable)

If you are running the Phase 6 image (`10_linux_led`, which does not include Dropbear SSH) or do not have an Ethernet adapter handy, you can push a cross-compiled binary (`fpga_led` or `led_server`, ~10–15 KB) directly over the `115200` baud serial UART using `gzip` + `base64`.

### Step 1 — Compress and base64-encode the binary on your Host PC

```bash
gzip -9 -c software/fpga_led/fpga_led | base64
```

Copy the printed base64 block (typically ~40 lines of ASCII text) to your clipboard.

### Step 2 — Decode and install on the DE10-Nano Serial Console

At the DE10-Nano serial prompt (`#`), run:

```bash
cat << 'EOF' | base64 -d | gunzip > /tmp/fpga_led && chmod +x /tmp/fpga_led && mv /tmp/fpga_led /usr/bin/fpga_led
<PASTE_YOUR_BASE64_LINES_HERE>
EOF
```

Verify the integrity of the transferred binary with `md5sum` or `sha256sum`:
```bash
# On both Host and Target:
sha256sum /usr/bin/fpga_led
```

> **Want a full deep-dive on deploying without SSH (including bringing up ad-hoc IPv6 + `nc` / `wget` over serial)?**
> Read the field guide at [`11_ethernet_hps_led/doc/deploying_without_ssh.md`](../11_ethernet_hps_led/doc/deploying_without_ssh.md).

---

## Related Documentation

- **Explanations:**
  - [Explanation: Embedded Linux Boot Architecture, Buildroot, FPGA Manager, and `/dev/mem` MMIO](explanation_phase6_embedded_linux_and_fpga_manager.md)
  - [Explanation: Embedded Networking, IPv6 SLAAC, Nanopb Protocol Buffers, and Async Web Gateways](explanation_phase7_networking_protobuf_and_dashboard.md)
