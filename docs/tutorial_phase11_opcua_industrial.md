# Phase 11 Tutorial — Industrial IIoT LED Control with OPC UA (`opc.tcp` & Part 14 UADP PubSub)

> **Series:** cvsoc — Stepping into advanced FPGA development on the DE10-Nano  
> **Phase:** 11 (`17_opcua_hps_led`)  
> **Difficulty:** Advanced — you have completed Phase 6 (`10_linux_led`) and Phase 7 (`11_ethernet_hps_led`) and understand Linux `/dev/mem` MMIO, dual-stack sockets, and Protocol Buffers  
> **Deep-Dive Explanation:** [Explanation — OPC UA Architecture, `opc.tcp` Wire Framing, UADP PubSub, and Comparison to `11_ethernet_hps_led`](explanation_phase11_opcua_and_protocol_comparison.md)  
> **Related How-To Guides:** [How-To: Extend the OPC UA Address Space and UADP PubSub Telemetry](howto_extend_opcua_address_space_and_pubsub.md) · [How-To: Access Remote Lab Board via SSH Jump Host (`dev-ubuntu`)](howto_remote_hardware_lab_access_via_ssh_jump_host.md) · [How-To: Deploy Over Serial and Ethernet](howto_deploy_to_board_over_serial_and_ethernet.md)

---

## What you will build

In [Phase 7 (`11_ethernet_hps_led`)](tutorial_phase7_ethernet.md), you built a custom 2-byte UDP protocol (`5005`), upgraded it to **Nanopb Protocol Buffers** (`5006`), and bridged it to a browser via a Python WebSocket gateway (`8080`).

In this tutorial (`17_opcua_hps_led`), you will upgrade the DE10-Nano into a standards-compliant **Industrial IoT (IIoT) Edge Controller** speaking **OPC UA (IEC 62541)**:

- An **ANSI C99 OPC UA Binary Server (`led_opcua_server`)** listening on `opc.tcp://<board-ip>:4840` that exposes the FPGA LED peripheral (`0xFF200000`) as a **self-describing, browsable Address Space** (Objects, Variables, DataTypes, AccessLevels, and callable RPC Methods)
- **Hardware-backed `UA_DataSource` callbacks** that map OPC UA `Read` (`631`), `Write` (`673`), and `Call` (`712`) requests directly to the Cyclone V Lightweight HPS-to-FPGA bridge (`0xFF200000`)—including individual bit-level boolean nodes (`fpga.led0` through `fpga.led7`)
- **Report-by-Exception Subscriptions (`MonitoredItems`)** where the server pushes `DataChangeNotification` (`829`) packets to subscribed clients the instant the LED register changes
- An **OPC UA Part 14 UADP (UA Datagram Protocol) PubSub publisher** broadcasting compact 24-byte cyclic telemetry frames over UDP port `4841`
- A **pure-Python OPC UA CLI client (`opcua_client.py`)** and **protocol benchmark (`compare_protocols.py`)** to inspect wire bytes and compare `17_opcua_hps_led` directly against `11_ethernet_hps_led`

```mermaid
flowchart LR
    subgraph PC ["🖥️ Industrial Client / PC"]
        CLI["opcua_client.py<br/>browse / read / write /<br/>call / subscribe"]
        SUB_UDP["pubsub-listen<br/>(UDP :4841)"]
    end

    subgraph NET ["Industrial Ethernet (IPv4 / IPv6)"]
        TCP["opc.tcp://:4840<br/>HEL → ACK → OPN → MSG<br/>(Browse / Read / Write / Call / Sub)"]
        UDP["OPC UA Part 14 UADP<br/>24-byte cyclic UDP frame<br/>port 4841"]
    end

    subgraph BOARD ["📟 DE10-Nano (Cyclone V SoC)"]
        direction TB
        SRV["led_opcua_server<br/>17-Node Address Space<br/>+ UA_DataSource Callbacks"]
        MMIO["mmap(/dev/mem)<br/>LWH2F @ 0xFF200000"]
        SYSFS["/sys/class/fpga_manager/<br/>fpga0/state"]
        FPGA["FPGA LED PIO<br/>(LED[7:0])"]

        SRV <--> MMIO <--> FPGA
        SRV <-- "read state" --- SYSFS
    end

    CLI <== "Stateful Control & Discovery" ==> TCP <==> SRV
    SRV == "Connectionless Telemetry" ==> UDP ==> SUB_UDP
```

---

## Prerequisites

| Requirement | Details |
|---|---|
| **Phase 6 & 7 complete** | `10_linux_led/de10_nano.rbf` bitstream available; familiarity with [`11_ethernet_hps_led`](tutorial_phase7_ethernet.md) |
| **Host tools** | `make`, `gcc`, `python3` (standard library only — zero `pip` packages required!) |
| **Docker (optional)** | `cvsoc/tools:1.0` or `arm-linux-gnueabihf-gcc` for `make server-cross` |
| **Board (optional for sim)** | Terasic DE10-Nano (`5CSEBA6U23I7`); all steps can also be run locally using `--sim` mode! |

---

## Step 1 — Run the Automated Integration Suite & Protocol Comparison on Your PC

Before deploying to hardware, verify the C server, the 17-node Address Space, the `opc.tcp` binary framing, subscriptions, and UADP PubSub on your host PC using hardware-simulation mode (`--sim`).

🖥️ **Host**
```bash
cd 17_opcua_hps_led
make test
```

Expected output:

```text
test_01_browse_address_space_discovery (__main__.TestDe10NanoOpcUa)
Browse (527) discovers Objects, Variables, and Methods without a .proto file. ... ok
test_02_read_and_write_led_pattern_register (__main__.TestDe10NanoOpcUa)
Read (631) and Write (673) update the simulated 0xFF200000 LED register. ... ok
test_03_individual_led_bit_nodes (__main__.TestDe10NanoOpcUa)
Writing `fpga.led0`..`fpga.led7` sets/clears individual bits in `fpga.led_pattern`. ... ok
test_04_method_calls_set_and_invert (__main__.TestDe10NanoOpcUa)
Call (712) executes `fpga.set_pattern` and `fpga.invert_pattern` on the server. ... ok
test_05_read_only_and_status_code_validation (__main__.TestDe10NanoOpcUa)
Server enforces standard OPC UA StatusCodes on invalid NodeIds, types, and ranges. ... ok
test_06_monitored_item_subscription_push (__main__.TestDe10NanoOpcUa)
Subscribing client receives immediate initial notification + push on state change. ... ok
test_07_part14_uadp_pubsub_telemetry (__main__.TestDe10NanoOpcUa)
Server broadcasts valid 24-byte OPC UA Part 14 UADP frames over UDP. ... ok
test_08_protocol_comparison_vs_phase7 (__main__.TestDe10NanoOpcUa)
Verifies wire size metrics comparing Phase 7 (11_ethernet_hps_led) and Phase 11. ... ok
----------------------------------------------------------------------
Ran 8 tests in 0.16s

OK
```

Now run `make compare` to inspect the side-by-side wire comparison between Phase 7 (`11_ethernet_hps_led`) and Phase 11 (`17_opcua_hps_led`):

🖥️ **Host**
```bash
make compare
```

```text
==================================================================================================
  WIRE PROTOCOL COMPARISON: 11_ethernet_hps_led (Phase 7) vs. 17_opcua_hps_led (Phase 11)
  Operation: Set FPGA LED PIO (0xFF200000) to pattern 0xA5 (165)
==================================================================================================
Protocol                                          Transport            Req   Resp Browse?  Push?
--------------------------------------------------------------------------------------------------
Phase 7.0 Raw UDP (11_ethernet_hps_led)           UDP :5005             2B     2B No       No
Phase 7.5 Nanopb Protobuf (11_ethernet_hps_led)   UDP :5006             3B     3B No       No
Phase 7.6 WebSocket JSON (11_ethernet_hps_led)    TCP/WS :8080         35B    42B No       Yes
Phase 11 OPC UA Client/Server (17_opcua_hps_led)  TCP :4840 (opc.tcp)   53B    32B Yes      Yes
Phase 11 OPC UA PubSub UADP (17_opcua_hps_led)    UDP :4841 (UADP)      0B    24B No       Yes
--------------------------------------------------------------------------------------------------
```

---

## Step 2 — Explore the OPC UA Server & Address Space Hands-On

You can run the hands-on walkthrough below against **either**:
- **Local Simulation (`HOST=127.0.0.1`):** Start `./software/opcua_server/led_opcua_server --sim` in one terminal, or
- **Your Physical DE10-Nano (`HOST=192.168.1.100` or `HOST=fe80::...%eth0`):** Cross-compile with `make server-cross` and copy `software/opcua_server/led_opcua_server` to the board (or boot the full SD card image from `make buildroot`).

### 2.1 Dynamic Discovery (`Browse` Service `527`) — Zero `.proto` Schema Needed!

In Phase 7.5 (`11_ethernet_hps_led`), if a client did not have a copy of [`led_command.proto`](../11_ethernet_hps_led/proto/led_command.proto), it had no way to know what fields or commands the board supported.

With OPC UA, ask the server to describe its own hardware tree using `browse`:

🖥️ **Host**
```bash
make browse HOST=127.0.0.1
```

Output:

```text
OPC UA Address Space on opc.tcp://127.0.0.1:4840 (17 nodes):
----------------------------------------------------------------------------------------------------
NodeId                       Class      BrowseName        Type      Access  Current Value
----------------------------------------------------------------------------------------------------
ns=1;s=de10nano              Object     DE10Nano          —         —       —
ns=1;s=fpga                  Object     FPGA              —         —       —
ns=1;s=fpga.led_pattern      Variable   LedPattern        Byte      R/W     0x0F (○○○○●●●●)
ns=1;s=fpga.led0             Variable   Led0              Boolean   R/W     true (ON)
ns=1;s=fpga.led1             Variable   Led1              Boolean   R/W     true (ON)
ns=1;s=fpga.led2             Variable   Led2              Boolean   R/W     true (ON)
ns=1;s=fpga.led3             Variable   Led3              Boolean   R/W     true (ON)
ns=1;s=fpga.led4             Variable   Led4              Boolean   R/W     false (OFF)
ns=1;s=fpga.led5             Variable   Led5              Boolean   R/W     false (OFF)
ns=1;s=fpga.led6             Variable   Led6              Boolean   R/W     false (OFF)
ns=1;s=fpga.led7             Variable   Led7              Boolean   R/W     false (OFF)
ns=1;s=fpga.bitstream_state  Variable   BitstreamState    String    R       operating (simulated)
ns=1;s=fpga.set_pattern      Method     SetPattern        —         Call    —
ns=1;s=fpga.invert_pattern   Method     InvertPattern     —         Call    —
ns=1;s=diag                  Object     Diagnostics       —         —       —
ns=1;s=diag.op_count         Variable   OperationCount    UInt32    R       0
ns=1;s=diag.uptime_sec       Variable   UptimeSeconds     UInt32    R       4
----------------------------------------------------------------------------------------------------
```

### 2.2 Reading and Writing Nodes (`Read` `631` & `Write` `673`)

Write `0xA5` (`10100101` in binary) to the 8-bit `fpga.led_pattern` register and inspect the raw `HELF`/`ACKF`/`OPNF`/`MSGF` packets on the wire with `--dump-hex`:

🖥️ **Host**
```bash
python3 client/opcua_client.py --host 127.0.0.1 --dump-hex write --node fpga.led_pattern --value 0xA5
```

Output:

```text
  [TX →] HELF ( 56 B): 48 45 4c 46 38 00 00 00 00 00 00 00 00 00 01 00 ...
  [RX ←] ACKF ( 28 B): 41 43 4b 46 1c 00 00 00 00 00 00 00 00 00 01 00 ...
  [TX →] OPNF ( 63 B): 4f 50 4e 46 3f 00 00 00 00 00 00 00 2f 00 00 00 ...
  [RX ←] OPNF ( 46 B): 4f 50 4e 46 2e 00 00 00 01 00 00 00 01 00 00 00 ...
  [TX →] MSGF ( 53 B): 4d 53 47 46 35 00 00 00 01 00 00 00 01 00 00 00 01 00 00 00 01 00 00 00 01 00 a1 02 03 01 00 10 00 00 00 66 70 67 61 2e 6c 65 64 5f 70 61 74 74 65 72 6e 03 a5
  [RX ←] MSGF ( 32 B): 4d 53 47 46 20 00 00 00 01 00 00 00 01 00 00 00 01 00 00 00 01 00 00 00 01 00 a4 02 00 00 00 00
Write fpga.led_pattern <- 0xA5: Good (0x00000000) | LED Register = 0xA5 (●○●○○●○●)
```

Now toggle a **single LED bit** (`LED[1]`) without touching the other 7 LEDs:

🖥️ **Host**
```bash
python3 client/opcua_client.py --host 127.0.0.1 write --node fpga.led1 --value true
```

```text
Write fpga.led1 <- true: Good (0x00000000) | LED Register = 0xA7 (●○●○○●●●)
```

### 2.3 Calling Server-Side Methods (`Call` Service `712`)

Invoke the `fpga.invert_pattern` RPC Method node on the board:

🖥️ **Host**
```bash
make call HOST=127.0.0.1
```

```text
Call fpga.invert_pattern() -> Good (0x00000000) | Output LED = 0x58
```

### 2.4 Report-by-Exception Subscriptions (`CreateSubscription` `787` & `Publish` `829`)

In Terminal 1, subscribe to `fpga.led_pattern` changes:

🖥️ **Host (Terminal 1)**
```bash
make subscribe HOST=127.0.0.1
```

In Terminal 2, write two different patterns (`0xAA` and `0x55`):

🖥️ **Host (Terminal 2)**
```bash
make write HOST=127.0.0.1 VAL=0xAA
make write HOST=127.0.0.1 VAL=0x55
```

Terminal 1 immediately receives the server-pushed `DataChangeNotification` chunks without polling:

```text
Created SubscriptionId=1 on 'fpga.led_pattern'. Waiting for 3 notifications...
  [Notification 1/3] ns=1;s=fpga.led_pattern -> 0x58
  [Notification 2/3] ns=1;s=fpga.led_pattern -> 0xAA
  [Notification 3/3] ns=1;s=fpga.led_pattern -> 0x55
```

### 2.5 Listening to OPC UA Part 14 UADP Binary PubSub Telemetry (`UDP :4841`)

Listen for the connectionless 24-byte UADP frames emitted periodically by `led_opcua_server`:

🖥️ **Host**
```bash
make pubsub-listen
```

```text
Listening for OPC UA Part 14 UADP frames on UDP port 4841...
  [1/3] From 127.0.0.1:49812 (24 B) | PubId=0x000A WriterId=1 Seq=14 | LED=0x55 OpCount=4 Uptime=14s
  [2/3] From 127.0.0.1:49812 (24 B) | PubId=0x000A WriterId=1 Seq=15 | LED=0x55 OpCount=4 Uptime=15s
  [3/3] From 127.0.0.1:49812 (24 B) | PubId=0x000A WriterId=1 Seq=16 | LED=0x55 OpCount=4 Uptime=16s
```

---

## Step 3 — Deploy to the Physical DE10-Nano

You can deploy `led_opcua_server` onto an existing Phase 6 / Phase 7 Linux boot on your DE10-Nano in seconds, or build a dedicated Buildroot SD card image where `S40led_opcua_server` starts automatically at boot.

### Option A: Rapid Iteration on a Running Board (`make server-cross` + `scp`)

🖥️ **Host**
```bash
cd 17_opcua_hps_led
make server-cross
scp software/opcua_server/led_opcua_server root@<board-ip>:/usr/bin/
ssh root@<board-ip> 'led_opcua_server &'
```

Then run any command from your PC against `<board-ip>` (IPv4 or IPv6 link-local `fe80::...%iface`):

🖥️ **Host**
```bash
make browse HOST=<board-ip>
make write  HOST=<board-ip> VAL=0xA5
```

### Option B: Full Bootable SD Card Image (`make buildroot`)

🖥️ **Host**
```bash
cd 17_opcua_hps_led
make all
make flash SDCARD=/dev/sdX
```

When the DE10-Nano boots:
1. `/etc/init.d/S30fpga_load` loads `/lib/firmware/de10_nano.rbf` into the Cyclone V FPGA via `fpga_mgr_load.ko`.
2. `/etc/init.d/S40led_opcua_server` starts `led_opcua_server` on `opc.tcp` port `4840` and UADP UDP port `4841`.

---

## Next Steps

- Read the **[Deep-Dive Explanation & Comparison to `11_ethernet_hps_led`](explanation_phase11_opcua_and_protocol_comparison.md)** to understand the internal byte layout of `HELF`/`ACKF`/`OPNF`/`MSGF`, `UA_DataSource` MMIO callbacks, UADP headers, and when to choose Raw UDP vs. Nanopb Protobuf vs. OPC UA.
- Follow the **[How-To Guide: Extend the OPC UA Address Space and UADP PubSub](howto_extend_opcua_address_space_and_pubsub.md)** to add new Variables and Methods and see how dynamic discovery compares to regenerating `.proto` stubs.
