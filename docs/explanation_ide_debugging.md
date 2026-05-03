# Explanation: IDE Debugging for Embedded Targets — VS Code and Neovim

> **Series:** cvsoc — Stepping into advanced FPGA development on the DE10-Nano
> **Type:** Explanation
> **Applies to:** Projects 06, 08, 09 — any project with a GDB server target

---

## Introduction

The `launch.json`, `tasks.json`, and `nvim-dap.lua` files in this repository configure two different editors to do the same thing: connect a graphical debugger to a bare-metal CPU running inside an FPGA or ARM SoC, using a GDB server as the hardware bridge.

This document explains the *concepts and reasoning* behind every setting and command. The goal is to give you enough understanding to adapt, extend, or recreate the configuration yourself — for a new project, a new target, or a new editor.

---

## 1. The Mental Model: Three Layers of Indirection

Before reading any configuration file, it helps to have a clear picture of the full call chain:

```
┌───────────────┐     DAP (JSON over stdio)    ┌──────────────────────┐
│  IDE          │ ──────────────────────────── │  Debug Adapter       │
│  (VS Code /   │                               │  (OpenDebugAD7 /     │
│   Neovim)     │                               │   cpptools)          │
└───────────────┘                               └──────────────────────┘
                                                          │
                                               GDB MI (text protocol)
                                                          │
                                               ┌──────────────────────┐
                                               │  GDB client          │
                                               │  (nios2-elf-gdb /    │
                                               │   arm-none-eabi-gdb) │
                                               │  running in Docker   │
                                               └──────────────────────┘
                                                          │
                                              TCP localhost:2345 / :3333
                                              (GDB Remote Serial Protocol)
                                                          │
                                               ┌──────────────────────┐
                                               │  GDB server          │
                                               │  (nios2-gdb-server / │
                                               │   OpenOCD)           │
                                               │  running in Docker   │
                                               └──────────────────────┘
                                                          │
                                                   USB-Blaster (JTAG)
                                                          │
                                               ┌──────────────────────┐
                                               │  Hardware            │
                                               │  (Nios II CPU /      │
                                               │   ARM Cortex-A9)     │
                                               └──────────────────────┘
```

There are four distinct pieces of software involved:

| Layer | Component | Role |
|-------|-----------|------|
| 1 | IDE (VS Code / Neovim) | Renders the UI. Knows nothing about GDB or hardware. |
| 2 | Debug Adapter (OpenDebugAD7) | Translates DAP ↔ GDB MI. The glue layer. |
| 3 | GDB client (nios2-elf-gdb) | Speaks the GDB remote protocol. Sends and interprets commands. |
| 4 | GDB server (nios2-gdb-server) | Owns the JTAG connection. Controls the CPU. |

**Why so many layers?** Each boundary solves a specific problem:

- The IDE/Adapter boundary exists so that any IDE can debug any language, as long as there is a DAP-compatible adapter for it. The IDE does not need to know what GDB is.
- The Adapter/GDB boundary exists because GDB predates modern IDEs by decades. The adapter translates the modern, structured DAP protocol into GDB's older text-based MI protocol.
- The GDB client/server boundary exists because GDB itself cannot speak JTAG. The server owns the hardware interface; the client is a pure software debugger. This split also means you can run the server closer to the hardware (inside Docker, where the USB stack is available) and the client anywhere.

---

## 2. The GDB Server — The Hardware Side

### What it actually does

`nios2-gdb-server` (for Nios II) and `openocd` (for ARM HPS) are hardware abstraction layers. They:

1. Connect to the CPU's debug port over JTAG via the USB-Blaster.
2. Expose that connection as a TCP socket using the **GDB Remote Serial Protocol (RSP)**.
3. Translate GDB commands (`G`, `M`, `c`, `s`, etc.) into JTAG transactions that halt the CPU, read registers, write memory, and set hardware breakpoints.

From GDB's perspective, the server looks identical to any other remote target — GDB does not know or care whether it is talking to hardware, a simulator, or a virtual machine.

### The Makefile targets

The `gdb-server` Makefile target in `06_nios2_interrupts/quartus/Makefile`:

```makefile
gdb-server: close-gdb-port usb-wsl
    docker run --rm --user $$(id -u):$$(id -g) -it --privileged \
      -p $(GDB_PORT):$(GDB_PORT) \
      -v $(REPO_ROOT):/work \
      -v $(UNAME_SHIM):/usr/local/bin/uname:ro \
      $(DOCKER_IMAGE) \
      bash -c '/opt/intelFPGA/quartus/bin/jtagd && sleep 2 && \
        nios2-gdb-server \
          --tcpport $(GDB_PORT) \
          --tcppersist \
          --tcpdebug; \
        kill $$(pgrep jtagd) 2>/dev/null || true'
```

Breaking this down:

- **`close-gdb-port`** — A prerequisite that stops any Docker container already listening on `localhost:2345`. This prevents "address already in use" errors on rapid restart cycles. It uses `docker ps --format '{{.ID}} {{.Ports}}'` to find and stop the offending container rather than killing the port directly, because the port is owned by Docker's proxy process, not by the container process itself.

- **`usb-wsl`** — Attaches the USB-Blaster device from Windows to the WSL2 network namespace using `usbipd.exe attach --wsl`. Without this, the Docker container running inside WSL2 cannot see the USB device.

- **`--privileged -p $(GDB_PORT):$(GDB_PORT)`** — The container needs `--privileged` to open USB devices via `jtagd`. The `-p` flag publishes the GDB port on the host's loopback interface so the GDB client (in a separate container) can reach it via `--network host`.

- **`-v $(UNAME_SHIM):/usr/local/bin/uname:ro`** — The Quartus toolchain calls `uname -r` and crashes if the kernel version string contains "microsoft" (as WSL2 kernel strings do). The shim replaces `uname` with a wrapper that returns a sanitised version string.

- **`jtagd && sleep 2`** — `jtagd` is Intel's JTAG daemon. It must be running before `nios2-gdb-server` attempts to enumerate JTAG devices. The `sleep 2` gives it time to initialise.

- **`--tcppersist`** — Keeps the GDB server alive after a GDB client disconnects. Without this, the server exits after the first debug session ends. This flag is what allows you to stop a debug session, recompile, and reconnect without restarting the server.

- **`--tcpdebug`** — Prints the raw RSP packets exchanged with the GDB client to stdout. Useful when diagnosing connection problems.

---

## 3. The Docker GDB Wrapper

The GDB client (`nios2-elf-gdb`, `arm-none-eabi-gdb`) must run inside the Docker image because:

1. **Architecture support:** `gdb-multiarch` on Ubuntu 22.04/24.04 is compiled without Nios II support. The correct GDB binary is only available inside the `cvsoc/quartus:23.1` image.
2. **Library compatibility:** The VS Code Remote Server requires glibc ≥ 2.28, but the Debian 9 base image inside the container only has glibc 2.24. Running VS Code *inside* the container as a DevContainer is not possible.

The wrapper script at `common/docker/nios2-elf-gdb-wrapper.sh` solves this:

```bash
exec docker run --rm -i \
  --network host \
  -v "${REPO_ROOT}:${REPO_ROOT}" \
  "${DOCKER_IMAGE}" \
  nios2-elf-gdb "$@"
```

Key decisions:

- **`--rm -i` (not `-it`)** — The container is ephemeral. `-i` keeps stdin open (GDB MI communicates over stdin/stdout). `-t` is deliberately omitted: the debug adapter connects via pipes, not a terminal, and a pseudo-TTY would corrupt the binary-adjacent MI protocol stream.

- **`--network host`** — The GDB client needs to connect to `localhost:2345`, which is where the *host* (WSL2) is publishing the GDB server port. `--network host` makes the container share the host's network namespace, so `localhost` inside the container is the same as `localhost` on the WSL2 host.

- **`-v "${REPO_ROOT}:${REPO_ROOT}"`** — The workspace is mounted at the *exact same absolute path* as on the host. This is critical: VS Code passes absolute host paths to GDB (e.g. `/home/balevision/workspace/bleviet/cvsoc/06_nios2_interrupts/software/app/nios2_interrupts.elf`). If the mount point were different inside the container, GDB would receive a path that does not exist, and source-level debugging would silently fail.

- **`"$@"`** — Every argument VS Code passes to the "GDB executable" is forwarded unchanged to `nios2-elf-gdb` inside the container. The debug adapter treats the wrapper as an opaque GDB binary — it does not know a container is involved.

---

## 4. The GDB Init Script (`nios2_interrupts.gdb`)

`make gdb-tui` passes this script to GDB with `-x scripts/nios2_interrupts.gdb`. It runs automatically after GDB starts, replacing a manual interactive session.

```gdb
target remote localhost:2345
load
break button_isr
continue
```

### `target remote localhost:2345`

Switches GDB from local execution mode to remote stub mode. After this command:

- GDB no longer tries to fork a process.
- All `read`/`write`/`continue`/`break` operations are translated to RSP packets and sent over TCP to the GDB server.
- The server proxies those packets to the CPU over JTAG.

This single command is what makes GDB talk to hardware instead of a local process.

### `load`

Downloads the ELF file (specified when GDB was invoked) into the target's memory. For Nios II, "memory" means the 32 KB on-chip RAM at `0x00000000`. The `load` command:

1. Parses the ELF's program headers to determine which segments to flash.
2. Sends `M` (write memory) RSP packets for each segment.
3. Sets the program counter to the ELF's entry point.

After `load`, the hardware contains exactly the binary you compiled. Any previous content of on-chip RAM is overwritten.

### `break button_isr`

Sets a software breakpoint at the start of the `button_isr` function. GDB resolves `button_isr` to a code address using the DWARF debug information embedded in the ELF (compiled with `-g3`). At that address, GDB writes a breakpoint instruction.

When the Nios II CPU executes that instruction, control transfers to the debug unit, which signals the GDB server, which signals the GDB client via RSP. GDB halts execution and the TUI (or IDE) shows the current source line.

> **Note on hardware vs. software breakpoints:** The Nios II/e Tiny core has a single hardware breakpoint comparator, which the GDB server itself reserves for its own internal use. All user breakpoints must be software breakpoints (the default for `break`). `hbreak` (hardware breakpoint) would compete with the server's comparator. ARM Cortex-A9 has six hardware comparators; `hbreak` is used in Project 09 precisely to avoid modifying code in OCRAM.

### Custom `define` commands

```gdb
define inspect-led
  echo === LED PIO DATA (0x00010010) ===\n
  x/1xw 0x10010
end
```

`define` creates a named GDB command. `x/1xw 0x10010` means:
- `x` — examine memory
- `/1` — one unit
- `x` — display as hexadecimal
- `w` — word (32-bit)
- `0x10010` — the LED PIO DATA register address from the memory map

These commands read hardware peripheral registers directly from GDB, without needing C code. This works because the Avalon bus is memory-mapped: the peripheral registers appear at fixed addresses in the CPU's address space, and GDB's memory reads go through the GDB server to the live hardware.

---

## 5. VS Code Debug Configuration (`launch.json`)

VS Code's debugger is built around a simple model: it reads a configuration object, hands it to a debug adapter, and renders whatever the adapter sends back. The `launch.json` file is VS Code's way of describing what adapter to start and how to configure it.

### Top-level fields

```json
{
    "name": "Toolchain: Debug Nios II (Project 06 — Interrupts)",
    "type": "cppdbg",
    "request": "launch",
    ...
}
```

- **`name`** — The string shown in the Run and Debug dropdown. The `Toolchain:` prefix signals that the debug session uses the in-container GDB toolchain (as opposed to `Host:` which uses the system GDB).

- **`type`** — Selects the debug adapter. `"cppdbg"` tells VS Code to start the `cpptools` extension's debug adapter (`OpenDebugAD7`). This is the DAP server that translates VS Code's UI interactions into GDB MI commands.

- **`request": "launch"`** — DAP supports two request types: `launch` (start a new process) and `attach` (connect to a running process). For embedded targets, `launch` is a misnomer — we are not launching a new process on the target. We are using `launch` because the adapter needs to start the GDB client process itself, and `launch` mode is what triggers that. The actual connection to hardware happens through `setupCommands`.

### GDB client selection

```json
"MIMode": "gdb",
"miDebuggerPath": "${workspaceFolder}/common/docker/nios2-elf-gdb-wrapper.sh",
```

- **`MIMode`** — Tells the adapter which GDB MI dialect to speak. `"gdb"` is the GNU GDB protocol. The alternative is `"lldb"` for LLVM's debugger.

- **`miDebuggerPath`** — The path to the GDB executable the adapter should spawn. Because `nios2-elf-gdb` is not available on the host, this points to the Docker wrapper script. The adapter treats the script as an opaque binary — it passes all standard GDB MI flags to it, and the script forwards them into the container.

### Connecting to the server: `setupCommands`

```json
"setupCommands": [
    { "text": "file ${workspaceFolder}/06_nios2_interrupts/software/app/nios2_interrupts.elf", "ignoreFailures": false },
    { "text": "target remote localhost:2345", "ignoreFailures": false },
    { "text": "load", "ignoreFailures": false },
    { "text": "break button_isr", "ignoreFailures": false }
],
```

`setupCommands` is a list of raw GDB MI commands that the adapter sends to GDB before handing control to VS Code's UI. They run in order and are equivalent to typing those commands manually at the GDB prompt.

- **`file <elf>`** — Loads the ELF's symbol table and debug information into GDB's internal state. This is separate from `load` (which writes the binary to hardware). `file` is needed first so that `target remote` can validate that the target's architecture matches the ELF.

- **`target remote localhost:2345`** — The same as in the GDB script: switches GDB to remote mode and connects to the GDB server. At this point, GDB and the GDB server perform a brief handshake and GDB learns the target's architecture and register layout from the server.

- **`load`** — Writes the ELF binary to on-chip RAM. Same semantics as in the `.gdb` script.

- **`break button_isr`** — Sets the initial breakpoint. VS Code's UI breakpoints (set by clicking in the gutter) are separate from these initial `setupCommands` breakpoints, but both end up as the same RSP `Z0` packets to the GDB server.

- **`"ignoreFailures": false`** — If set to `true`, the adapter continues even if the command fails. `false` means a failure aborts the debug session with an error message. For the connection and load commands, failure is never recoverable, so `false` is correct.

### The ARM-specific `hbreak` (Project 09)

```json
{ "text": "hbreak main", "ignoreFailures": false }
```

For the ARM Cortex-A9, `hbreak` (hardware breakpoint) is used instead of `break` (software breakpoint). The reason is subtle: the firmware is loaded into OCRAM (on-chip SRAM). Software breakpoints work by writing a trap instruction into the code. If the code is in OCRAM, this works. But hardware breakpoints use the CPU's debug comparators without modifying memory — they are preferred for ROM or flash targets where memory cannot be modified at runtime. Using `hbreak` here is a deliberate choice to avoid any risk of corrupting the OCRAM contents.

### `monitor arm mcr 15 0 1 0 0 0` (Project 09)

```json
{ "text": "monitor arm mcr 15 0 1 0 0 0", "ignoreFailures": true }
```

`monitor` is a GDB command that passes a raw string to the GDB server (OpenOCD in this case) rather than interpreting it as a GDB command. OpenOCD executes it as its own command language.

`arm mcr 15 0 1 0 0 0` writes to the ARM coprocessor register `CP15 c1 c0 0` (the System Control Register). Setting bit 0 to 0 disables the MMU. This is done before loading the ELF because a running MMU would translate the physical OCRAM address into a virtual address, and GDB's `load` command uses physical addresses. Disabling the MMU ensures the write goes exactly where intended.

`ignoreFailures: true` is set because this command only matters when the ARM CPU has previously run code with the MMU enabled. On a cold reset, the MMU is already off and the command is a no-op. Failing silently is preferable to aborting the session.

### `launchCompleteCommand`

```json
"launchCompleteCommand": "exec-continue"
```

After all `setupCommands` complete, the adapter sends one final command to GDB before declaring the session "launched" to VS Code. Two values are common:

- **`"exec-continue"`** — sends `-exec-continue` (GDB MI equivalent of `continue`). The target starts running immediately.
- **`"exec-run"`** — sends `-exec-run`. Only valid for native (non-remote) debugging where GDB itself launched the process. Using `exec-run` on a remote target causes an error.
- **`"none"`** — sends nothing. The target stays halted. Use this when you want GDB to stop at the very beginning before any code runs.

For this project, `exec-continue` is the right choice: the target should run freely and stop only when a button press triggers the `button_isr` breakpoint.

### `preLaunchTask`

```json
"preLaunchTask": "gdb-server: Start Nios II GDB server (Project 06)"
```

Before spawning GDB, VS Code runs this named task from `tasks.json`. The task starts `make gdb-server`. VS Code waits for the task to signal readiness before proceeding to the `setupCommands` sequence.

The mechanism that signals readiness is the `problemMatcher`, explained in the next section.

---

## 6. VS Code Task Orchestration (`tasks.json`)

VS Code tasks are shell commands that VS Code can run, monitor, and use as dependencies.

```json
{
    "label": "gdb-server: Start Nios II GDB server (Project 06)",
    "type": "shell",
    "command": "make gdb-server USBIPD_BUSID=${config:cvsoc.usbBusId}",
    "options": { "cwd": "${workspaceFolder}/06_nios2_interrupts/quartus" },
    "isBackground": true,
    "runOptions": { "instanceLimit": 1 },
    "presentation": { "reveal": "always", "panel": "dedicated", "clear": true },
    "problemMatcher": {
        "pattern": { "regexp": "^$" },
        "background": {
            "activeOnStart": true,
            "beginsPattern": ".",
            "endsPattern": "Listening on port"
        }
    }
}
```

### `isBackground: true`

Marks the task as a long-running background process. Without this, VS Code would wait for the task process to exit before considering it "done." Because `make gdb-server` runs a Docker container that keeps running indefinitely, it never exits. `isBackground: true` tells VS Code to use the `problemMatcher`'s background section to detect readiness instead of waiting for process exit.

### `problemMatcher` and the background pattern

The `problemMatcher` serves two roles in VS Code tasks:

1. **Pattern matching for diagnostics** — normally used to parse compiler error output (e.g., `file:line:column: error: message`). The `pattern.regexp: "^$"` here matches no lines — there are no compiler errors to parse.

2. **Background task lifecycle detection** — when `isBackground: true`, VS Code uses `background.beginsPattern` and `background.endsPattern` to determine when the task has transitioned from "starting" to "ready".

The lifecycle:
- `beginsPattern: "."` — matches any non-empty line. Marks the task as "active" as soon as the first line of output appears.
- `endsPattern: "Listening on port"` — marks the task as "ready" when this exact string appears in stdout.

When VS Code sees "Listening on port" in the task's output, it signals the debug launch to proceed with the `setupCommands` sequence. This is the synchronisation point: VS Code guarantees that `target remote localhost:2345` is not attempted until the server has confirmed it is accepting connections.

Without this synchronisation, a fast machine might attempt to connect before `jtagd` has initialised, causing a "Connection refused" error.

### `runOptions.instanceLimit: 1`

Prevents VS Code from starting a second GDB server task if one is already running. Without this limit, pressing F5 twice in quick succession would try to start a second Docker container, which would fail because port 2345 is already bound. The limit ensures the existing task is reused.

### `${config:cvsoc.usbBusId}`

A workspace-level setting variable, defined in `.vscode/settings.json`:

```json
{ "cvsoc.usbBusId": "2-4" }
```

This is the `usbipd` bus identifier for the USB-Blaster device on the Windows host. It varies per machine. Using a VS Code setting variable externalises the machine-specific value from the committed `tasks.json` file — the committed file references `${config:cvsoc.usbBusId}`, and each developer sets the correct value in their own (gitignored) `settings.json`.

---

## 7. The Debug Adapter Protocol (DAP) — The Shared Backbone

Both VS Code and Neovim use the same underlying specification: the **Debug Adapter Protocol**, designed by Microsoft and now an open standard.

### What DAP is

DAP is a JSON-based, request/response protocol with notifications, transported over stdio. An IDE (the "client") sends requests like:

```json
{ "command": "setBreakpoints", "arguments": { "source": { "path": "main.c" }, "breakpoints": [{ "line": 42 }] } }
```

The debug adapter (the "server") responds with:

```json
{ "body": { "breakpoints": [{ "verified": true, "line": 42 }] } }
```

The adapter then translates each DAP request into GDB MI commands and parses GDB's responses back into DAP events.

### `cpptools` and `OpenDebugAD7`

`ms-vscode.cpptools` (VS Code) and Mason's `cpptools` package (Neovim) both provide the same binary: `OpenDebugAD7`. This is Microsoft's DAP adapter for C/C++ debugging. It:

- Accepts DAP messages on stdin/stdout.
- Spawns a GDB client subprocess (whatever `miDebuggerPath` points to).
- Forwards GDB MI output from GDB to DAP events and back.

Because both editors use `OpenDebugAD7` as the adapter, the GDB-level behaviour is identical between VS Code and Neovim. The difference between the two editors is only in:
1. How the configuration is expressed (JSON vs. Lua).
2. How the UI is rendered (VS Code's sidebar panels vs. `nvim-dap-ui` windows).
3. How background tasks are orchestrated (VS Code's tasks system vs. a manually started terminal).

---

## 8. Neovim `nvim-dap` Configuration (`nvim-dap.lua`)

`nvim-dap` is the Neovim plugin that implements the DAP client. It does what VS Code's built-in debugger does — sends DAP requests, receives events, and drives the UI — but is configured in Lua rather than JSON.

The configuration file at `06_nios2_interrupts/doc/nvim-dap.lua` is a **LazyVim plugin spec** — a Lua table that LazyVim's plugin manager (`lazy.nvim`) merges into the plugin graph at startup.

### The two-table design: adapters vs. configurations

`nvim-dap` uses two separate registries:

```lua
dap.adapters.cppdbg = { ... }    -- HOW to start the debug adapter process
dap.configurations.c = { ... }   -- WHAT to debug (one entry per project/target)
```

This separation mirrors DAP's own architecture:

- **Adapters** describe the *process*: what binary to run, what arguments to pass, what transport to use. One adapter type can serve many configurations.
- **Configurations** describe the *session*: which ELF to load, which GDB to use, which server to connect to. Many configurations can reuse the same adapter.

In VS Code, this separation is implicit — the `type` field in `launch.json` selects the adapter, and the rest of the object is the configuration. In nvim-dap, it is explicit.

### The adapter definition

```lua
dap.adapters.cppdbg = {
    id      = "cppdbg",
    type    = "executable",
    command = vim.fn.stdpath("data")
        .. "/mason/packages/cpptools/extension/debugAdapters/bin/OpenDebugAD7",
}
```

- **`type = "executable"`** — nvim-dap will launch the adapter as a subprocess and communicate with it over stdio. The alternative is `"server"` (connect to a TCP port) or `"pipe"` (communicate over a named pipe).
- **`command`** — the absolute path to `OpenDebugAD7`. `vim.fn.stdpath("data")` returns Neovim's data directory (typically `~/.local/share/nvim`). Mason installs packages there, so this path is machine-independent.
- **`id = "cppdbg"`** — the name used in configurations' `type` field to select this adapter.

### The critical field: `miDebuggerServerAddress`

```lua
miDebuggerServerAddress = "localhost:2345",
```

This field does not appear in the VS Code `launch.json` for Nios II. The reason it is *required* in nvim-dap is subtle and worth understanding deeply.

When `cpptools` starts in `launch` mode, it inspects the target ELF to decide whether this is a native or remote debug session. If it finds an ELF compiled for the host architecture (e.g. x86_64 on an x86_64 machine), it treats the session as native and skips the `target remote` step. If the ELF is cross-compiled (e.g. for Nios II), `cpptools` falls back to... refusing to start, because it cannot run a Nios II binary locally.

Setting `miDebuggerServerAddress` is the escape hatch: it explicitly tells `cpptools` "this is a remote session, connect GDB to this address instead of trying to run the binary locally." With this field set, `cpptools` issues `target remote localhost:2345` automatically as part of its own startup sequence, *before* the `setupCommands` run.

In VS Code, `cpptools` performs the same check, but the extension has additional logic that detects the `nios2-elf-gdb-wrapper.sh` as a non-native GDB and skips the architecture check. In nvim-dap, the adapter is invoked more directly and that heuristic does not apply — so the field must be explicit.

### `mason-nvim-dap` and auto-registration

```lua
{
    "jay-babu/mason-nvim-dap.nvim",
    optional = true,
    opts = {
        ensure_installed = { "cppdbg" },
        handlers = {},
    },
},
```

`mason-nvim-dap` bridges two plugins: Mason (which installs packages like `cpptools`) and nvim-dap (which needs adapters registered). The `handlers = {}` (empty table) activates the default handler for all installed Mason DAP packages, which auto-registers each package's adapter in `dap.adapters`.

`ensure_installed = { "cppdbg" }` tells Mason to install the `cpptools` package if it is not already present. The package name `"cppdbg"` is the Mason identifier (distinct from the `cpptools` extension name used elsewhere).

The explicit adapter registration in the second plugin spec:

```lua
if not dap.adapters.cppdbg then
    dap.adapters.cppdbg = { ... }
end
```

is a guard against a race condition: if this plugin spec's `config` function runs before `mason-nvim-dap`'s handlers have fired (which can happen on first launch, before Mason has downloaded the package), the adapter would be unregistered and the debug session would fail with `adapter not found: cppdbg`. The `if not` check ensures the adapter is registered regardless of load order.

### LazyVim plugin merge semantics: `optional = true`

```lua
{ "mfussenegger/nvim-dap", optional = true, config = function() ... end }
```

LazyVim's `extras.dap.core` already includes `nvim-dap` as a dependency. Declaring it again with `optional = true` tells `lazy.nvim` to merge this spec *into* the existing one rather than adding a duplicate. Without `optional = true`, `lazy.nvim` would error on the duplicate.

The `config` override replaces LazyVim's `config` for `nvim-dap`. LazyVim's `extras.dap.core` sets `config = function() end` (empty) because it puts all its setup logic in the `keys` table (keymaps) and `dependencies`. Overriding `config` therefore only adds our adapter registration and configuration without losing any LazyVim keybindings, since those live in a separate table.

### LazyVim keybindings

LazyVim's `extras.dap.core` provides these default bindings (referenced in the README):

| Key | nvim-dap action | VS Code equivalent |
|-----|-----------------|--------------------|
| `<leader>dc` | `dap.continue()` | F5 |
| `<leader>db` | `dap.toggle_breakpoint()` | F9 |
| `<leader>dB` | `dap.set_breakpoint(condition)` | Conditional breakpoint dialog |
| `<leader>do` | `dap.step_over()` | F10 |
| `<leader>di` | `dap.step_into()` | F11 |
| `<leader>dO` | `dap.step_out()` | Shift+F11 |
| `<leader>dr` | `dap.repl.open()` | Debug Console |
| `<leader>du` | `dapui.toggle()` | Toggle sidebar panels |

`dap.continue()` has dual behaviour: if no session is active, it starts one (prompting for a configuration if multiple exist). If a session is active, it resumes execution. This is why `<leader>dc` is used both to *start* and to *continue* a debug session.

---

## 9. Side-by-Side Comparison

The following table maps every conceptual element between the VS Code and Neovim configurations:

| Concept | VS Code (`launch.json`) | Neovim (`nvim-dap.lua`) |
|---------|-------------------------|--------------------------|
| Adapter selection | `"type": "cppdbg"` | `type = "cppdbg"` in configuration + `dap.adapters.cppdbg` |
| Adapter binary path | Resolved internally by the `cpptools` VS Code extension | Explicit: `vim.fn.stdpath("data") .. "/mason/.../OpenDebugAD7"` |
| Adapter installation | Via `ms-vscode.cpptools` VS Code extension | Via Mason: `ensure_installed = { "cppdbg" }` |
| GDB client binary | `"miDebuggerPath"` | `miDebuggerPath` |
| ELF path | `"program"` | `program` |
| Working directory | `"cwd"` | `cwd` |
| GDB dialect | `"MIMode": "gdb"` | `MIMode = "gdb"` |
| Remote server address | Implicit (derived from `setupCommands`) | Explicit: `miDebuggerServerAddress = "localhost:2345"` |
| Pre-session GDB commands | `"setupCommands": [...]` | `setupCommands = { ... }` |
| Resume after setup | `"launchCompleteCommand": "exec-continue"` | `launchCompleteCommand = "exec-continue"` |
| GDB server lifecycle | Automatic via `preLaunchTask` + `problemMatcher` | Manual: run `make gdb-server` in a terminal first |
| Breakpoints UI | Sidebar Breakpoints panel | `nvim-dap-ui` Breakpoints buffer |
| Variable inspection | Sidebar Variables panel | `nvim-dap-ui` Scopes buffer |
| Debug console | Debug Console panel | `dap.repl` (`:DapToggleRepl`) |

### The key difference: task automation

The most significant practical difference is server lifecycle management:

- **VS Code** automates this entirely via `preLaunchTask`. Pressing F5 starts the GDB server, waits for it to be ready (via `problemMatcher`), then connects GDB — all transparently.
- **Neovim** requires you to start `make gdb-server` in a separate terminal manually before invoking `<leader>dc`. There is no built-in equivalent of VS Code's background task system in nvim-dap (though it can be scripted via `dap.listeners`).

Everything at the GDB level — the `setupCommands`, the `load`, the breakpoints, the remote protocol — is identical. The GDB server does not know or care which editor initiated the session.

---

## Adapting This Setup to a New Target

When adding a new project to this debugging setup, the changes required are:

| What changes | VS Code | Neovim |
|-------------|---------|--------|
| New ELF path | New `launch.json` configuration | New entry in `dap.configurations.c` |
| New GDB server port | New `tasks.json` task + update `target remote` in `setupCommands` | Update `miDebuggerServerAddress` + `setupCommands` |
| Different GDB binary | Different `miDebuggerPath` wrapper script | Different `miDebuggerPath` |
| Different initial breakpoint | Different `break` in `setupCommands` | Different `break` in `setupCommands` |
| New architecture (e.g., RISC-V) | New wrapper script + possibly new adapter type | New adapter entry in `dap.adapters` |

The GDB server itself (`nios2-gdb-server`, OpenOCD) is always the component closest to the hardware. Everything above it — the GDB client, the Docker wrapper, the debug adapter, the IDE configuration — is software that can be replaced or reconfigured without touching the target hardware or its JTAG setup.
