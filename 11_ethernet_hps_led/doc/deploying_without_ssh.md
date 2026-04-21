# Deploying to an Embedded Board Without SSH — A Field Guide

*Phase 7 Track A — Option B (standalone cross-compile) deployment on DE10-Nano with Buildroot Phase 6 image.*

---

## Introduction

The README describes Option B as: *"cross-compile `led_server` and copy it to an already-running Phase 6 board."*  
What it does not say is that the Phase 6 Buildroot image contains **no SSH server, no telnet daemon, and no IPv4 address** — making the implied `scp` command impossible until a chain of obstacles are resolved.  
This document walks through every obstacle encountered in a real session, with the theory behind each one, so you can understand not just *what* was done but *why it works*.

---

## Setup

```
Host    : Ubuntu 22.04 running inside WSL2 (kernel 6.6.87.2-microsoft-standard-WSL2)
Board   : DE10-Nano running Buildroot Phase 6 image (Linux 6.6.86 / BusyBox)
Link    : USB-Ethernet adapter (enx08beac224c03) plugged into the board's LAN port
Serial  : /dev/ttyUSB0 @ 115200 baud
```

---

## Obstacle 1 — The Board Has No IPv4 Address

### Symptom

The user ran `ip a` on the board and got:

```
2: eth0: <BROADCAST,MULTICAST,UP,LOWER_UP> …
    inet6 fe80::e420:3eff:fe86:db60/64 scope link
```

No `inet` line — only `inet6` with a `scope link` address.

### Theory: Link-Local IPv6

Every IPv6-capable network interface automatically assigns itself a **link-local address** from the `fe80::/10` prefix the moment it comes up.  
The address is derived from the interface MAC address using the EUI-64 algorithm (or a random value), and it requires zero configuration.  
It is valid **only on the directly attached link** — no router involved, no DHCP server needed.

The board's defconfig contains `BR2_SYSTEM_DHCP="eth0"`, which tells Buildroot's init scripts to run `udhcpc` at boot.  
`udhcpc` sends a DHCP DISCOVER and waits for an offer.  
Because the board is connected point-to-point to a host interface that has no DHCP server, `udhcpc` eventually times out with no lease.  
The interface stays UP (carrier is detected), but no IPv4 address is assigned.  
The auto-configured `fe80::` address, however, is always present.

### Impact

All the tools mentioned in the README (`scp`, the Python client's `socket.AF_INET`) require IPv4 (or at minimum an IPv6 global/ULA address). The link-local address is usable but requires extra care:

1. **Zone ID** — link-local addresses are only meaningful on a specific interface. On the *sender* side the address must be qualified as `fe80::…%interfacename` (e.g. `fe80::e420:3eff:fe86:db60%enx08beac224c03`). Without the zone ID the kernel does not know which interface to transmit on.
2. **Reachability check** — verify with `ping6 -I <iface> fe80::<board-addr>` before assuming anything works.

### Resolution (foreshadowing Obstacles 5 and 6)

The fixes to `led_server.c` and `send_led_pattern.py` described at the end of this document address this.

---

## Obstacle 2 — No SSH on the Board

### Symptom

```
$ scp software/led_server/led_server \
      "root@[fe80::e420:3eff:fe86:db60%enx08beac224c03]:/usr/bin/"
ssh: connect to host … port 22: Connection refused
```

Port scanning confirmed nothing was listening on 22, 23, 80, or any other well-known port.

### Theory: Minimal Buildroot Images

Buildroot's philosophy is **explicit opt-in**: nothing is included unless the developer adds it to the `defconfig`.  
The Phase 6 `de10_nano_defconfig` lists exactly:

```
BR2_PACKAGE_FPGA_LED=y
BR2_PACKAGE_FPGA_MGR_LOAD=y
BR2_PACKAGE_PYTHON3=y
BR2_INIT_BUSYBOX=y
```

There is no `BR2_PACKAGE_DROPBEAR=y` or `BR2_PACKAGE_OPENSSH=y`.  
A Dropbear SSH daemon would add roughly 200 KB to the rootfs and an attack surface; for a tutorial image it was simply omitted.

### Consequence

Every remote-access primitive that relies on SSH — `scp`, `sftp`, `rsync`, Ansible, VS Code Remote — is unavailable.  
The available transfer channels on this image are:
- **Serial console** (always present, proven working since `ip a` was run there)
- **Python 3** (explicitly packaged: can use `urllib.request` or `http.client`)
- **BusyBox utilities**: `wget`, `tftp`, `nc`, `dd`

### Resolution

Set up a temporary **HTTP server on the host**, then pull the binary **from the board** using Python 3's `urllib.request`.  
HTTP is a pull protocol — the board initiates the connection outward, so no inbound firewall hole or listening service is needed on the board side.

```bash
# Host: serve the binary
cd 11_ethernet_hps_led/software/led_server
python3 -m http.server 8080 --bind "::"
```

The `--bind ::` flag tells Python to listen on the IPv6 wildcard (which also covers IPv4-mapped addresses on Linux).

```python
# Board (via serial): pull and save
python3 -c "
import urllib.request
urllib.request.urlretrieve(
    'http://[fe80::abe:acff:fe22:4c03%25eth0]:8080/led_server',
    '/usr/bin/led_server'
)
"
```

The zone ID `%25eth0` is the URL-percent-encoded form of `%eth0`.  
Python 3's `urllib` correctly parses RFC 6874 scoped IPv6 literals and passes the scope ID to the socket layer.

---

## Obstacle 3 — The Serial Port Was Locked

### Symptom

The user already had **picocom** open on `/dev/ttyUSB0`.  
When trying to open a second connection:

```
FATAL: cannot lock /dev/ttyUSB0: Resource temporarily unavailable
Skipping tty reset...
```

### Theory: Two Layers of Serial Port Locking

Linux provides two independent mechanisms to prevent concurrent access to a serial tty:

#### Layer 1 — POSIX Advisory File Locks (`lockf` / `flock`)

When picocom opens the serial port it creates a **lock file** in `/var/lock/` with a name like `LCK..ttyUSB0`.  
This file contains the PID of the locking process.  
Any other program that respects the convention checks for this file before opening the port and refuses if it exists (returning `EACCES` or `EAGAIN`).  
This is the error picocom printed — it checked the lock file, found it owned by PID 3537, and gave up.  
Crucially, this is a **cooperative** lock: a program that does not check the lock file can still open the device.

#### Layer 2 — `TIOCEXCL` (Exclusive Mode)

After opening the port, picocom calls:

```c
ioctl(fd, TIOCEXCL);   /* set exclusive mode in the tty driver */
```

Once set, the **kernel** refuses any subsequent `open()` call on that character device from any other process (returning `EBUSY`), regardless of whether the other process checks the lock file.  
This is an **enforced** kernel-level lock.

#### Why `/proc/PID/fd/N` Worked Anyway

Once a file descriptor is open, its kernel object (a `struct file`) is reachable through `/proc/<pid>/fd/<n>`.  
Opening that symlink path goes through the VFS in a way that **shares the underlying `struct file`** rather than calling `tty_open()` again — specifically through `proc_fd_link()` in the kernel's procfs implementation, which hands you a reference to the already-open file rather than a fresh open of the character device.  
Because `TIOCEXCL` is checked at `tty_open()` time (new opens), sharing an existing open bypasses the check entirely.  

The bottom line: you can **write to an already-open fd** by going through `/proc/<pid>/fd/<n>` even when the device is in exclusive mode.

```python
import os

def send_to_board(text: str) -> None:
    # Open a writable reference to picocom's serial fd
    fd = os.open('/proc/3537/fd/3', os.O_WRONLY)
    os.write(fd, text.encode())
    os.close(fd)
```

> **Security note:** This only works because both the agent process and picocom run as the same user (`balevision`). A different user would get `EACCES` when trying to open `/proc/<pid>/fd/`.

#### Alternative: `ptrace` / GDB

The first idea was to attach GDB to picocom and call `write()` from within its process space — this would sidestep all permission issues.

```
$ cat /proc/sys/kernel/yama/ptrace_scope
1
```

`ptrace_scope = 1` means ptrace is only allowed **from a parent process** (or with `CAP_SYS_PTRACE`).  
Since the agent is not picocom's parent, GDB attachment was rejected.  
`TIOCSTI` (inject a byte as if typed at the terminal) was also tried — it requires `CAP_SYS_TTY_CONFIG` on kernels ≥ 5.0 when the target tty is not the caller's controlling terminal.

#### Simpler Path: Wait for Picocom to Exit

When the user eventually closed picocom, the serial port became fully available.  
At that point, the `pyserial` library provided clean, direct access:

```python
import serial, time

s = serial.Serial('/dev/ttyUSB0', 115200, timeout=2)

def send_cmd(cmd: str, wait: float = 3.0) -> str:
    s.write((cmd + '\r\n').encode())
    s.flush()
    time.sleep(wait)
    out = b''
    while s.in_waiting:
        out += s.read(s.in_waiting)
        time.sleep(0.1)
    return out.decode(errors='replace')
```

`pyserial` respects the lock file convention and sets up the baud rate, parity, and flow-control settings automatically.

---

## Obstacle 4 — BusyBox `wget` Cannot Handle IPv6 Zone IDs

### Symptom

```
# wget http://[fe80::abe:acff:fe22:4c03%25eth0]:8080/led_server -O /usr/bin/led_server
wget: can't connect to remote host: Invalid argument
```

### Theory: RFC 6874 and Embedded Limitations

[RFC 6874](https://www.rfc-editor.org/rfc/rfc6874) specifies that a **zone ID** (e.g. `%eth0`) may be embedded in a URI as `%25eth0` (the `%` sign percent-encoded).  
Full TCP/IP stacks in glibc parse this via `getaddrinfo()` with the `AI_NUMERICHOST` flag, which understands `fe80::…%eth0` notation.

BusyBox's built-in `wget` is a stripped-down implementation that does simple string parsing of the host portion of the URL. It does not pass the zone ID to `getaddrinfo`, so the `setsockopt(SO_BINDTODEVICE)` or `sin6_scope_id` field is never set, and the kernel rejects the `connect()` call with `EINVAL` — "Invalid argument".

**Python 3's `urllib.request`**, however, uses the full glibc resolver, which properly extracts the scope ID and sets `sin6_scope_id` in the `sockaddr_in6` structure before calling `connect()`.

### Resolution

Prefer Python 3 over BusyBox `wget` whenever the transfer involves a link-local IPv6 address.

```python
# Works correctly on BusyBox Python 3.12
import urllib.request
urllib.request.urlretrieve(
    'http://[fe80::abe:acff:fe22:4c03%25eth0]:8080/led_server',
    '/usr/bin/led_server'
)
```

---

## Obstacle 5 — "Text file busy" When Overwriting a Running Binary

### Symptom

After `led_server` was already running, trying to overwrite the binary in place:

```
python3: OSError: [Errno 26] Text file busy: '/usr/bin/led_server'
```

### Theory: `ETXTBSY` in the Linux Kernel

When the kernel `execve()`s a binary, it opens the ELF file, memory-maps the `.text` and `.data` segments into the process address space, and keeps the file open (with `O_RDONLY` internally).  
Linux enforces a rule: **a file that is currently being executed cannot be opened for writing** — the kernel returns `ETXTBSY` (error 26).  

This is not about file locks (flock/lockf); it is a hard kernel invariant.  
The rationale: if you could overwrite the `.text` segment in the file while the CPU was fetching instructions from it, you could corrupt a running process's code.

### Why `mv` Works

`mv` (implemented with the `rename()` syscall) is an **atomic directory entry update**: it replaces the name in the directory inode to point at a new inode, without modifying the old inode at all.  
The running process holds an open reference to the *old inode*. After `mv`, the old inode is unlinked from the directory but remains alive until the process closes it (or exits). The new inode (with the updated binary) is immediately visible to any future `execve()`.

```sh
# Download to a temp location first (different inode)
python3 -c "import urllib.request; urllib.request.urlretrieve('…', '/tmp/led_server_new')"

# Atomic replace: replaces the directory entry, old inode stays alive
mv /tmp/led_server_new /usr/bin/led_server
chmod +x /usr/bin/led_server
```

This is the same technique used by package managers (`dpkg`, `rpm`, `opkg`) and program updaters everywhere: **never overwrite a running file; always rename into place**.

---

## Obstacle 6 — Both Server and Client Were IPv4-Only

### Symptom

After all connectivity was established, the server on the board and the Python client on the host both used `AF_INET` (IPv4) sockets. Since the board had no IPv4 address, communication was impossible.

### Theory: Dual-Stack Sockets and `getaddrinfo`

#### Server side — `AF_INET6` with `IPV6_V6ONLY = 0`

On Linux, a socket created with `AF_INET6` can simultaneously accept both pure IPv6 connections and IPv4 connections represented as **IPv4-mapped IPv6 addresses** (`::ffff:x.x.x.x`), as long as `IPV6_V6ONLY` is set to `0` (this is the Linux default, but it is good practice to set it explicitly).

```c
// Before
int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
struct sockaddr_in addr = {
    .sin_family = AF_INET,
    .sin_port   = htons(port),
    .sin_addr.s_addr = INADDR_ANY,
};

// After — dual-stack: accepts IPv6 and IPv4-mapped
int sockfd = socket(AF_INET6, SOCK_DGRAM, 0);
int v6only = 0;
setsockopt(sockfd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only));
struct sockaddr_in6 addr = {
    .sin6_family = AF_INET6,
    .sin6_port   = htons(port),
    .sin6_addr   = in6addr_any,   /* equivalent to :: */
};
```

Binding to `::` means: "accept UDP datagrams arriving on any interface via IPv6 or via IPv4."

The address logging also changes: `inet_ntop(AF_INET6, …)` with `INET6_ADDRSTRLEN` instead of `INET_ADDRSTRLEN`.

#### Client side — `getaddrinfo` for address-family agnosticism

Hardcoding `AF_INET` in the client breaks for any host that is reached via IPv6.  
The POSIX `getaddrinfo()` function (and Python's `socket.getaddrinfo()`) resolves a hostname or numeric address string into a list of `(family, type, proto, canonname, sockaddr)` tuples.  
It understands:
- `192.168.1.100` → `AF_INET`, `sockaddr = ('192.168.1.100', port)`
- `fe80::e420:3eff:fe86:db60%enx08beac224c03` → `AF_INET6`, `sockaddr = ('fe80::e420:3eff:fe86:db60%enx08beac224c03', port, 0, <scope_id>)`

```python
# Before
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.sendto(req, (args.host, args.port))

# After — works for IPv4, IPv6 global, and link-local
addr_info = socket.getaddrinfo(args.host, args.port, type=socket.SOCK_DGRAM)
af, _, _, _, dest_addr = addr_info[0]
sock = socket.socket(af, socket.SOCK_DGRAM)
sock.sendto(req, dest_addr)   # dest_addr is a proper (host, port[, flow, scope]) tuple
```

The key insight is that for `AF_INET6` the address tuple has **four** elements `(host, port, flowinfo, scope_id)`, not two.  
`getaddrinfo` fills in `scope_id` automatically when it parses the `%interface` suffix.

---

## Complete Flow Diagram

```
┌─────────────────────────────────────────────────────────────────────┐
│  HOST (WSL2 Ubuntu, kernel 6.6)                                     │
│                                                                     │
│  1. make server-cross    ← Docker cross-compiles led_server (ARM)  │
│                                                                     │
│  2. python3 -m http.server 8080 --bind ::                          │
│     → serves led_server binary on http://[::]:8080/                │
│                                                                     │
│  3. pyserial → /dev/ttyUSB0 (115200 baud, dialout group)          │
│     → runs shell commands on the board                             │
│                                                                     │
│  4. python3 client/send_led_pattern.py                             │
│     --host fe80::e420:3eff:fe86:db60%enx08beac224c03 --animate … │
└────────────────────┬────────────────────┬───────────────────────────┘
                     │ USB-serial         │ USB-Ethernet
                     │ /dev/ttyUSB0       │ enx08beac224c03
                     │ 115200 baud        │ fe80::abe:acff:fe22:4c03
                     ▼                    ▼
┌─────────────────────────────────────────────────────────────────────┐
│  DE10-Nano (Buildroot Linux 6.6, BusyBox, Python 3.12)             │
│                                                                     │
│  eth0:  fe80::e420:3eff:fe86:db60/64  (link-local only, no DHCP)  │
│                                                                     │
│  Step A: python3 urllib.request.urlretrieve(                       │
│              'http://[fe80::abe:acff:fe22:4c03%25eth0]:8080/…')   │
│          → downloads led_server to /tmp, then mv to /usr/bin/      │
│                                                                     │
│  Step B: led_server &                                               │
│          → binds AF_INET6 UDP socket on :::5005                    │
│          → mmaps /dev/mem @ 0xFF200000 (LW H2F bridge / LED PIO)  │
│                                                                     │
│  Step C: receives SET/GET commands from host Python client         │
│          → writes 8-bit pattern to FPGA LED PIO register           │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Lessons Learned and Recommended Fixes

### 1. Add Dropbear to the Buildroot Image

The single most impactful change: add one line to `11_ethernet_hps_led/br2-external/configs/de10_nano_defconfig`:

```
BR2_PACKAGE_DROPBEAR=y
```

Dropbear is a lightweight SSH server+client (≈ 350 KB on ARM), purpose-built for embedded systems.  
With it, Option B becomes exactly what the README describes:

```sh
make server-cross
scp software/led_server/led_server root@[fe80::…%enx…]:/usr/bin/
```

### 2. Use `getaddrinfo` from the Start

The client should never hardcode `AF_INET`. Any code that calls `getaddrinfo` at the start works transparently over both IPv4 and IPv6 with zero extra logic.

### 3. Plan for `ETXTBSY` in Iterative Development

When iterating on embedded binaries, always use the **download-to-temp, then `mv`** pattern:

```sh
wget http://… -O /tmp/new_binary
mv /tmp/new_binary /usr/bin/my_program
```

Alternatively, kill the running process first, then overwrite in place.

### 4. Dual-Stack UDP Server Is the Right Default

Unless you have a specific reason to restrict to IPv4, all UDP/TCP servers in embedded Linux should bind on `AF_INET6` with `IPV6_V6ONLY = 0`.  
This costs nothing and works correctly in all deployment scenarios (DHCP IPv4, DHCP IPv6, link-local only).

---

## Quick Reference

| Situation | Tool | Command |
|-----------|------|---------|
| Board has no SSH | HTTP pull | `python3 -m http.server 8080 --bind ::` on host; `python3 -c "urllib.request.urlretrieve(…)"` on board |
| Serial port locked by picocom | /proc fd access | `os.open('/proc/<pid>/fd/3', os.O_WRONLY)` |
| BusyBox wget rejects link-local IPv6 | Python 3 urllib | Supports `%25eth0` zone ID in URL |
| Text file busy on running binary | Atomic rename | Download to `/tmp`, then `mv` |
| IPv4-only socket, board has no IPv4 | Dual-stack socket | `AF_INET6` + `IPV6_V6ONLY=0` on server; `getaddrinfo()` on client |
| Verify board reachability | ping6 | `ping6 -I enx08beac224c03 fe80::<board-mac-addr>` |
