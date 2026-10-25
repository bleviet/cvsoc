# Phase 11: Industrial IIoT with OPC UA — Client/Server (`opc.tcp`) & Part 14 PubSub (`17_opcua_hps_led`)

> **Full Step-by-Step Tutorial:** [Tutorial — Industrial IIoT on the DE10-Nano with OPC UA (`opc.tcp` & UADP PubSub)](../../docs/tutorial_phase11_opcua_industrial.md)  
> **Deep-Dive Explanation & Comparison to `11_ethernet_hps_led`:** [Explanation — OPC UA Architecture, `opc.tcp` Wire Framing, UADP PubSub, and Comparison with Raw UDP & Nanopb Protobuf](../../docs/explanation_phase11_opcua_and_protocol_comparison.md)  
> **Related How-To Guide:** [How-To: Extend the OPC UA Address Space with New Variables, Methods, and PubSub Telemetry (Compared to Protobuf)](../../docs/howto_extend_opcua_address_space_and_pubsub.md)

---

## Overview

In **Phase 7 ([`11_ethernet_hps_led`](../../11_ethernet_hps_led/doc/README.md))**, we controlled the DE10-Nano's FPGA LEDs over Ethernet using a custom 2-byte UDP protocol (`5005`), upgraded the wire serialization to **Nanopb Protocol Buffers** (`5006`), and built a Python **WebSocket-to-UDP Gateway** (`8080`).

However, in industrial automation (factories, SCADA systems, PLCs, and IIoT edge controllers), external clients do not have your custom [`led_command.proto`](../../11_ethernet_hps_led/proto/led_command.proto) schema file or custom Python scripts. Instead, industrial systems standardize on **OPC UA (IEC 62541)**, which provides:

1. **A Self-Describing Information Model (Address Space):** Any OPC UA client can connect to `opc.tcp://<board-ip>:4840` and invoke the **Browse Service (`527`)** to dynamically discover all Objects, Variables, DataTypes, AccessLevels (`R` vs `R/W`), and callable RPC Methods exposed by the DE10-Nano—with **zero shared schema files**.
2. **Hardware-Backed `UA_DataSource` Callbacks:** Reading or writing `ns=1;s=fpga.led_pattern` or individual bits `ns=1;s=fpga.led0..7` triggers C callbacks that read or write the physical FPGA LED PIO register at `0xFF200000` (via `/dev/mem`) on the Lightweight HPS-to-FPGA AXI bridge.
3. **Report-by-Exception Subscriptions (`MonitoredItems`):** Instead of polling over UDP, clients subscribe to `fpga.led_pattern` and receive server-pushed `DataChangeNotification` (`829`) messages the instant another client or method modifies the FPGA register.
4. **OPC UA Part 14 UADP PubSub (`UDP :4841`):** Alongside the stateful `opc.tcp` control plane, `led_opcua_server` broadcasts compact 24-byte connectionless **UADP (UA Datagram Protocol)** binary frames over UDP for cyclic real-time telemetry.

---

## Project Structure

```text
17_opcua_hps_led/
├── Makefile                        — Build, test, compare protocols, and run OPC UA CLI commands
├── doc/
│   └── README.md                   — Project overview & quick reference (this file)
├── software/
│   └── opcua_server/
│       ├── Makefile                — Compiles led_opcua_server (host gcc or ARM cross-compiler)
│       ├── ua_micro.h              — Embedded ANSI C99 OPC UA Binary (opc.tcp + UADP) API
│       ├── ua_micro.c              — IEC 62541-6 framing, Browse/Read/Write/Call/Sub & UADP encoder
│       └── led_opcua_server.c      — DE10-Nano Address Space & /dev/mem (or --sim) MMIO callbacks
├── client/
│   ├── opcua_client.py             — Pure-Python dual-stack (IPv4/IPv6) OPC UA & UADP CLI client
│   ├── compare_protocols.py        — Side-by-side wire size & hex comparison vs 11_ethernet_hps_led
│   └── test_opcua.py               — Automated 8-test integration suite (runs against C server --sim)
└── br2-external/                   — Buildroot config with S30fpga_load and S40led_opcua_server
```

---

## Quick Start: Run Automated Tests & Protocol Comparison (No Board Required)

You can compile the C OPC UA server in hardware-simulation mode (`--sim`), run the full 8-test integration suite over localhost TCP/UDP sockets, and inspect the wire comparison against [`11_ethernet_hps_led`](../../11_ethernet_hps_led/doc/README.md) on your PC:

```bash
cd 17_opcua_hps_led
make test
make compare
```

---

## DE10-Nano OPC UA Address Space (`opc.tcp://<board-ip>:4840`)

| NodeId (`ns=1;s=...`) | NodeClass | BrowseName | DataType | Access | Hardware Mapping / Behavior |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `ns=1;s=de10nano` | `Object` | `DE10Nano` | — | — | Root device container under `Objects` |
| `ns=1;s=fpga` | `Object` | `FPGA` | — | — | Avalon-MM peripherals on LWH2F bridge (`0xFF200000`) |
| `ns=1;s=fpga.led_pattern` | `Variable` | `LedPattern` | `Byte` (`3`) | `R/W` | 8-bit LED PIO register (`0x00`..`0xFF`) at `0xFF200000` |
| `ns=1;s=fpga.led0` .. `fpga.led7` | `Variable` | `Led0` .. `Led7` | `Boolean` (`1`) | `R/W` | Individual bit access to physical `LED[0]`..`LED[7]` |
| `ns=1;s=fpga.bitstream_state` | `Variable` | `BitstreamState` | `String` (`12`) | `R` | Reads `/sys/class/fpga_manager/fpga0/state` (`"operating"`) |
| `ns=1;s=fpga.set_pattern` | `Method` | `SetPattern` | — | `Call` | Validates range (`0x00`..`0xFF`) and writes LED PIO |
| `ns=1;s=fpga.invert_pattern` | `Method` | `InvertPattern` | — | `Call` | Bitwise-inverts (`~reg & 0xFF`) the current LED PIO |
| `ns=1;s=diag` | `Object` | `Diagnostics` | — | — | Server operational telemetry container |
| `ns=1;s=diag.op_count` | `Variable` | `OperationCount` | `UInt32` (`7`) | `R` | Total hardware MMIO write/call operations executed |
| `ns=1;s=diag.uptime_sec` | `Variable` | `UptimeSeconds` | `UInt32` (`7`) | `R` | Elapsed seconds since `led_opcua_server` started |

---

## Summary Comparison: `11_ethernet_hps_led` vs. `17_opcua_hps_led`

| Feature | Phase 7.0 Raw UDP (`11_ethernet_hps_led`) | Phase 7.5 Nanopb Protobuf (`11_ethernet_hps_led`) | Phase 11 OPC UA `opc.tcp` (`17_opcua_hps_led`) | Phase 11 OPC UA PubSub UADP (`17_opcua_hps_led`) |
| :--- | :--- | :--- | :--- | :--- |
| **Transport & Port** | UDP `:5005` | UDP `:5006` | TCP `:4840` (`opc.tcp`) | UDP `:4841` (UADP) |
| **`SET_PATTERN(0xA5)` Req / Resp** | **2 B / 2 B** | **3 B / 3 B** | **53 B / 32 B** *(after `HEL`/`OPN`)* | **0 B / 24 B** *(cyclic push)* |
| **Schema Required on Client?** | Hardcoded byte offsets | Must share `led_command.proto` | **None** — self-discovered via `Browse` (`527`) | Standard IEC 62541-14 UADP |
| **Granular Bit Addressing?** | No (full byte only) | No (full byte only) | **Yes** (`fpga.led0` .. `fpga.led7`) | Payload configurable |
| **Remote Procedure Calls (RPC)?** | Custom command byte | Enum `CommandType` | **First-class `Method` nodes (`Call` `712`)** | N/A (unidirectional) |
| **Push Notifications?** | No (client must poll) | No (client must poll) | **Yes (`MonitoredItem` `DataChange` `829`)** | **Yes (cyclic UDP datagrams)** |
| **Industrial SCADA / PLC Ready?** | No | No (requires custom gateway) | **Yes (Native IEC 62541 standard)** | **Yes (Native PLC/TSN PubSub)** |
