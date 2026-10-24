# How-to: Extend the Nanopb Protobuf Schema and Web Dashboard with New Commands

> **Series:** cvsoc — Stepping into advanced FPGA development on the DE10-Nano  
> **Type:** How-to Guide (Problem-oriented)  
> **Difficulty:** Intermediate  
> **Applies to:** Phase 7.5 & Phase 7.6 (`11_ethernet_hps_led` — [`tutorial_phase7_ethernet.md`](tutorial_phase7_ethernet.md))

---

## Overview

In Phase 7.5 and 7.6 (`11_ethernet_hps_led`), LED commands are defined in [`11_ethernet_hps_led/proto/led_command.proto`](../11_ethernet_hps_led/proto/led_command.proto), decoded in C on the DE10-Nano using **Nanopb** (`software/led_server/led_server_pb.c`), sent from Python using `client/send_led_pattern_pb.py`, and visualized in the browser via `dashboard/gateway.py` and `dashboard/dashboard.html`.

This guide walks you through adding a **new command or field**—for example, a `TOGGLE_PATTERN` (`XOR`) command or a `duration_ms` auto-off timer—end-to-end across the schema, C server, Python client, WebSocket gateway, and unit tests.

---

## Step 1 — Edit `proto/led_command.proto`

Open [`11_ethernet_hps_led/proto/led_command.proto`](../11_ethernet_hps_led/proto/led_command.proto). Because Protocol Buffers (`proto3`) identify fields by **tag number** (`= 1`, `= 2`, `= 3`), never change the tag numbers of existing fields! Always append new enum values or optional message fields with unused tag numbers:

```protobuf
enum CommandType {
  SET_PATTERN    = 0;
  GET_PATTERN    = 1;
  INVERT_PATTERN = 2;   // <-- NEW: bitwise invert current LED register (~pattern)
}

message LedCommand {
  CommandType command     = 1;
  uint32      pattern     = 2;
  uint32      duration_ms = 3;  // <-- NEW: optional hold time in ms (0 = permanent)
}
```

---

## Step 2 — Regenerate the Nanopb (C) and Python Stubs

From `11_ethernet_hps_led/`, run the `proto` Makefile target (or invoke `protoc` with the Nanopb plugin):

```bash
cd 11_ethernet_hps_led
make proto
```

This single command regenerates three files from `proto/led_command.proto`:
1. `software/led_server/led_command.pb.h` — Updates `led_CommandType` enum (`led_CommandType_INVERT_PATTERN = 2`) and adds `uint32_t duration_ms;` to `typedef struct _led_LedCommand`.
2. `software/led_server/led_command.pb.c` — Updates the Nanopb field descriptor table (`PB_FIELD` macros).
3. `client/led_command_pb2.py` — Updates the Python Protobuf classes used by both the CLI client and the web dashboard gateway.

> **Note if adding `string` or `bytes` fields to `.proto`:**
> Scalar fields (`uint32`, `bool`, `enum`) are statically sized automatically by Nanopb. If you ever add a `string` or `bytes` field, create a companion `proto/led_command.options` file specifying a fixed maximum buffer size (for example, `led.LedResponse.version max_size:16`) so Nanopb generates a fixed `char version[16]` array instead of a `pb_callback_t` stream callback!

---

## Step 3 — Handle the New Command/Field in the C Server (`led_server_pb.c`)

Open [`11_ethernet_hps_led/software/led_server/led_server_pb.c`](../11_ethernet_hps_led/software/led_server/led_server_pb.c) and locate the `switch (cmd.command)` dispatch block inside `handle_packet()`:

```c
switch (cmd.command) {
case led_CommandType_SET_PATTERN:
    if (cmd.pattern > 0xFFu) {
        resp.status  = led_StatusCode_ERR_INVALID_DATA;
        resp.pattern = *reg & 0xFFu;
    } else {
        *reg         = cmd.pattern & 0xFFu;
        resp.status  = led_StatusCode_OK;
        resp.pattern = *reg & 0xFFu;
    }
    break;

case led_CommandType_GET_PATTERN:
    resp.status  = led_StatusCode_OK;
    resp.pattern = *reg & 0xFFu;
    break;

/* ── NEW: Handle INVERT_PATTERN ── */
case led_CommandType_INVERT_PATTERN: {
    uint32_t inverted = (~(*reg)) & 0xFFu;
    *reg         = inverted;
    resp.status  = led_StatusCode_OK;
    resp.pattern = *reg & 0xFFu;
    break;
}

default:
    resp.status  = led_StatusCode_ERR_UNKNOWN_CMD;
    resp.pattern = *reg & 0xFFu;
    break;
}
```

Cross-compile the updated Protobuf C server:

```bash
cd 11_ethernet_hps_led
make server-pb-cross
```

---

## Step 4 — Expose the New Command in the Python Client and Web Gateway

### 1. In `client/send_led_pattern_pb.py`

Add a helper function that constructs and encodes `LedCommand(command=led_command_pb2.INVERT_PATTERN)`:

```python
def build_invert_command() -> bytes:
    msg = led_command_pb2.LedCommand(
        command=led_command_pb2.INVERT_PATTERN
    )
    return msg.SerializeToString()
```

### 2. In `dashboard/gateway.py`

Add a REST route (for example, `POST /api/invert`) that sends the command over UDP via `run_in_executor` and broadcasts both the TX/RX wire hex and new LED state to all connected WebSocket clients:

```python
async def handle_invert(request: web.Request) -> web.Response:
    # 1. Build request & broadcast TX packet to WebSocket packet log
    tx_bytes = build_invert_command()
    await broadcast_ws(request.app, {
        "type": "packet", "dir": "tx",
        "hex": tx_bytes.hex(" "), "summary": "INVERT_PATTERN"
    })

    # 2. Execute UDP call in thread pool
    loop = asyncio.get_running_loop()
    status, pattern, rx_bytes = await loop.run_in_executor(
        None, send_raw_pb, request.app["board_host"], request.app["board_port"], tx_bytes
    )

    # 3. Broadcast RX packet + new LED state
    await broadcast_ws(request.app, {
        "type": "packet", "dir": "rx",
        "hex": rx_bytes.hex(" "), "summary": f"OK pattern=0x{pattern:02X}"
    })
    await broadcast_ws(request.app, {"type": "state", "pattern": pattern})
    return web.json_response({"status": status, "pattern": pattern})
```

---

## Step 5 — Verify with Unit Tests (No Hardware Required)

Before deploying to the DE10-Nano, add a test case to [`11_ethernet_hps_led/client/test_protocol_pb.py`](../11_ethernet_hps_led/client/test_protocol_pb.py) and [`11_ethernet_hps_led/dashboard/test_gateway.py`](../11_ethernet_hps_led/dashboard/test_gateway.py) and run the test suites on your host PC:

```bash
cd 11_ethernet_hps_led
make test-pb
pytest dashboard/test_gateway.py
```

Once all host tests pass, deploy `software/led_server/led_server_pb` to the board (see [How-to: Deploy Binaries to the DE10-Nano](howto_deploy_to_board_over_serial_and_ethernet.md)).

---

## Related Documentation

- **Explanation:**
  - [Explanation: Embedded Networking, IPv6 SLAAC, Nanopb Protocol Buffers, and Async Web Gateways](explanation_phase7_networking_protobuf_and_dashboard.md)
- **Tutorial:**
  - [Phase 7 Tutorial — Ethernet LED Control over UDP (§7.5 & §7.6)](tutorial_phase7_ethernet.md)
