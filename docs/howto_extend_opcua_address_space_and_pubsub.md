# How-to: Extend the OPC UA Address Space with New Variables, Methods, and PubSub Telemetry (Compared to Protobuf)

> **Series:** cvsoc — Stepping into advanced FPGA development on the DE10-Nano  
> **Type:** How-to Guide (Problem-oriented)  
> **Difficulty:** Intermediate-Advanced  
> **Applies to:** Phase 11 ([`17_opcua_hps_led`](../17_opcua_hps_led/doc/README.md) — [`tutorial_phase11_opcua_industrial.md`](tutorial_phase11_opcua_industrial.md))  
> **Counterpart Guide:** Compare directly with [How-to: Extend the Nanopb Protobuf Schema and Web Dashboard (`11_ethernet_hps_led`)](howto_extend_protobuf_and_web_dashboard.md)

---

## Overview

When you add a new hardware register or command to a networked FPGA+SoC design, how much code do you have to touch on the server and client?

In [How-to: Extend the Nanopb Protobuf Schema (`11_ethernet_hps_led`)](howto_extend_protobuf_and_web_dashboard.md), adding a new command (`INVERT_PATTERN`) or parameter (`duration_ms`) required **four coordinated steps across both server and client**:
1. Edit `proto/led_command.proto` and assign new field tag numbers.
2. Run `make proto` (`protoc` + `nanopb_generator.py`) to regenerate C (`led_command.pb.c`/`.h`) and Python (`led_command_pb2.py`) stubs.
3. Update the C server `switch (cmd.command)` handler and recompile.
4. Update the Python client and WebSocket gateway with the new `led_command_pb2` fields.

In **Phase 11 (`17_opcua_hps_led`)**, because OPC UA is **self-describing at runtime**, adding a new Variable or Method node to the C server (`led_opcua_server.c`) requires **zero `.proto` code generation and zero client code changes**—existing OPC UA clients (`opcua_client.py`, UaExpert, or a SCADA system) automatically discover the new node, its `DataType`, its `AccessLevel`, and its `Description` the next time they call `browse`!

This guide shows you step by step how to:
1. Add a new Read/Write **Variable Node** (`ns=1;s=fpga.blink_ms` of type `UInt32`) to [`led_opcua_server.c`](../17_opcua_hps_led/software/opcua_server/led_opcua_server.c).
2. Add a new callable **Method Node** (`ns=1;s=fpga.rotate_left`) that circularly shifts the 8-bit FPGA LED pattern left by 1 bit (`(reg << 1) | (reg >> 7)`).
3. Verify that [`opcua_client.py`](../17_opcua_hps_led/client/opcua_client.py) discovers, reads, writes, and calls the new nodes **without modifying a single line of Python client code**.

---

## Step 1 — Add Backing State and `UA_DataSource` Callbacks for a New Variable Node

Open [`17_opcua_hps_led/software/opcua_server/led_opcua_server.c`](../17_opcua_hps_led/software/opcua_server/led_opcua_server.c).

### 1.1 Add the field to `HardwareContext`

Locate `typedef struct { ... } HardwareContext;` and add `uint32_t blink_ms;`:

```c
typedef struct {
    volatile uint32_t *led_reg;       /* Points to /dev/mem mmap or sim_reg */
    uint32_t           sim_reg;       /* Backing store in --sim mode */
    bool               sim_mode;      /* True when running without /dev/mem */
    uint32_t           op_count;      /* Total hardware operations executed */
    uint32_t           blink_ms;      /* <-- NEW: configurable blink period (ms) */
    time_t             start_time;    /* Server boot timestamp */
} HardwareContext;
```

### 1.2 Write the `read` and `write` DataSource callbacks

Add a range-checked read/write callback pair (`10` ms to `10000` ms, returning `UA_STATUSCODE_BADOUTOFRANGE` if out of bounds):

```c
static UA_StatusCode ds_read_blink_ms(void *user_ctx,
                                      const UA_NodeId *node_id,
                                      UA_Variant *out_val)
{
    (void)node_id;
    HardwareContext *ctx = (HardwareContext *)user_ctx;
    out_val->type      = UA_TYPE_UINT32;
    out_val->value.u32 = ctx->blink_ms;
    return UA_STATUSCODE_GOOD;
}

static UA_StatusCode ds_write_blink_ms(void *user_ctx,
                                       const UA_NodeId *node_id,
                                       const UA_Variant *in_val)
{
    (void)node_id;
    HardwareContext *ctx = (HardwareContext *)user_ctx;
    if (in_val->type != UA_TYPE_UINT32) {
        return UA_STATUSCODE_BADTYPEMISMATCH;
    }
    if (in_val->value.u32 < 10u || in_val->value.u32 > 10000u) {
        return UA_STATUSCODE_BADOUTOFRANGE;
    }
    ctx->blink_ms = in_val->value.u32;
    return UA_STATUSCODE_GOOD;
}
```

### 1.3 Register `fpga.blink_ms` in `build_de10nano_address_space()`

Inside `build_de10nano_address_space()`, register the new Variable node under the `"fpga"` parent object:

```c
    UA_DataSource blink_ds = {
        .read  = ds_read_blink_ms,
        .write = ds_write_blink_ms
    };
    UA_Server_addDataSourceVariableNode(
        server,
        UA_NODEID_STRING(1, "fpga.blink_ms"),
        "fpga",
        "BlinkPeriodMs",
        "Blink Period (ms)",
        "Configurable blink interval in milliseconds (10..10000 ms)",
        UA_TYPE_UINT32,
        UA_ACCESSLEVELMASK_READWRITE,
        blink_ds,
        ctx
    );
```

---

## Step 2 — Add a New Callable Method Node (`fpga.rotate_left`)

In [`17_opcua_hps_led/software/opcua_server/led_opcua_server.c`](../17_opcua_hps_led/software/opcua_server/led_opcua_server.c), define the C callback that circularly shifts the 8-bit LED pattern left by 1 bit:

```c
static UA_StatusCode method_rotate_left(void *user_ctx,
                                        const UA_NodeId *method_id,
                                        const UA_Variant *in_arg,
                                        UA_Variant *out_arg)
{
    (void)method_id;
    (void)in_arg;
    HardwareContext *ctx = (HardwareContext *)user_ctx;
    uint8_t cur     = hw_read_leds(ctx);
    uint8_t rotated = (uint8_t)(((cur << 1) | (cur >> 7)) & 0xFFu);
    hw_write_leds(ctx, rotated);

    out_arg->type     = UA_TYPE_BYTE;
    out_arg->value.u8 = hw_read_leds(ctx);
    return UA_STATUSCODE_GOOD;
}
```

Then register `fpga.rotate_left` in `build_de10nano_address_space()`:

```c
    UA_Server_addMethodNode(
        server,
        UA_NODEID_STRING(1, "fpga.rotate_left"),
        "fpga",
        "RotateLeft",
        "Circular Rotate Left (1 Bit)",
        "Rotates the 8-bit FPGA LED pattern left by 1 bit: (reg << 1) | (reg >> 7)",
        method_rotate_left,
        ctx
    );
```

---

## Step 3 — Recompile Only the Server and Discover the New Nodes Automatically

Recompile `led_opcua_server`:

```bash
cd 17_opcua_hps_led
make server-host      # or `make server-cross` for the physical DE10-Nano
```

Notice what you **did not** have to do:
- You did **not** edit a `.proto` file.
- You did **not** run `protoc` or `nanopb_generator.py`.
- You did **not** edit [`client/opcua_client.py`](../17_opcua_hps_led/client/opcua_client.py).

Start the server (`./software/opcua_server/led_opcua_server --sim`) and run the **unmodified** client:

```bash
# 1. Browse discovers `fpga.blink_ms` and `fpga.rotate_left` automatically!
python3 client/opcua_client.py --host 127.0.0.1 browse --parent fpga

# 2. Invoke the new `fpga.rotate_left` Method node:
python3 client/opcua_client.py --host 127.0.0.1 call --method fpga.rotate_left
```

---

## Step 4 — Side-by-Side Workflow Comparison: Extending Protobuf (`11_ethernet_hps_led`) vs. Extending OPC UA (`17_opcua_hps_led`)

| Task | Extending Nanopb Protobuf ([`11_ethernet_hps_led`](howto_extend_protobuf_and_web_dashboard.md)) | Extending OPC UA Address Space (`17_opcua_hps_led`) |
| :--- | :--- | :--- |
| **1. Define schema** | Edit `proto/led_command.proto` & assign unique tag number | Register node in `build_de10nano_address_space()` (`UA_Server_add...`) |
| **2. Code generation** | Run `make proto` (`protoc` + `nanopb_generator.py`) | **None** |
| **3. Server implementation** | Add `case` to `switch (cmd.command)` in `led_server_pb.c` | Attach `UA_DataSource` or `UA_MethodCallback` function pointer in `led_opcua_server.c` |
| **4. Client update required?** | **Yes** — must redistribute `led_command_pb2.py` and add CLI flags | **No** — existing clients discover new nodes dynamically via `Browse` (`527`) |
| **5. Wire size of new call** | **2–4 bytes** over UDP | **~48 bytes** over `opc.tcp` |
| **6. Best suited for** | Tightly coupled embedded/cloud teams optimizing bandwidth | Multi-vendor industrial systems (SCADA/PLC/HMI) requiring plug-and-play discovery |
