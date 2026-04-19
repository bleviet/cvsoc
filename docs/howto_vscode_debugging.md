# How-to: Debug Nios II and ARM HPS with VS Code

> **Series:** cvsoc — Stepping into advanced FPGA development on the DE10-Nano  
> **Type:** How-to Guide  
> **Difficulty:** Intermediate  
> **Applies to:** Phase 5 (Projects 08 and 09)

---

This guide explains how to connect the Visual Studio Code (VS Code) graphical debugger to the Nios II and ARM Cortex-A9 embedded targets.

The approach is a **split-process model**:

* The **GDB server** (which drives the USB-Blaster) runs inside the `cvsoc/quartus:23.1` Docker container, started from a terminal with `make gdb-server` or `make openocd`.
* The **GDB client** is also run inside `cvsoc/quartus:23.1`, but as a thin wrapper script (`nios2-elf-gdb-wrapper.sh` / `arm-none-eabi-gdb-wrapper.sh`) that VS Code spawns as a subprocess — no VS Code Server or DevContainer required.

> **Why not `gdb-multiarch` for Nios II, and why not a DevContainer?**
>
> * `gdb-multiarch` on Ubuntu 22.04/24.04 is compiled without Nios II support (`set arch nios2` → *Undefined item*).
> * The `cvsoc/quartus:23.1` image is based on Debian 9 (glibc 2.24, GLIBCXX 3.4.22). The VS Code Remote Server requires glibc ≥ 2.28 and GLIBCXX ≥ 3.4.25, so the DevContainer approach fails at startup.
>
> The wrapper scripts work around both constraints: the correct architecture-specific GDB binary runs inside the container, but VS Code stays entirely on the host.

---

## Prerequisites

Before starting, ensure you have:

1. **Completed Phase 5:** You must be able to successfully compile and run either `08_nios2_debug` or `09_hps_debug` from the command line.
2. **VS Code Installed:** Running on your host machine (Linux native or Windows via WSL Remote).
3. **C/C++ Extension:** The official `ms-vscode.cpptools` extension installed in VS Code.
4. **Docker running on the host:** The wrapper scripts use `docker run` — the same requirement as all other `make` targets in this repo.

---

## How it works

The repository includes two ready-made wrapper scripts in `common/docker/`:

| Script | GDB binary inside container | Used for |
|--------|-----------------------------|----------|
| `nios2-elf-gdb-wrapper.sh` | `nios2-elf-gdb` (GDB 13.2) | Project 08 — Nios II |
| `arm-none-eabi-gdb-wrapper.sh` | `arm-none-eabi-gdb` (GDB 7.12) | Project 09 — HPS ARM |

Each script mounts the workspace at the **same absolute path** inside the container, so every path VS Code passes to GDB (ELF file, source paths) resolves identically inside Docker — no path translation needed. `--network host` lets the containerised GDB client connect to the GDB server on `localhost:<port>`.

---

## Step 1 — Program the FPGA

If you haven't already:

```bash
# Project 09
cd 09_hps_debug/quartus && make program-sof

# Project 08
cd 08_nios2_debug/quartus && make program-sof
```

---

## Step 2 — Start the GDB server (optional)

> **TIP:** Steps 2 and 3 can be combined — pressing **F5** in VS Code automatically starts the GDB server as a background task and waits for it to be ready before connecting the GDB client. You only need to run this step manually if you want to verify the server is working before launching the debugger.

**Project 09 — HPS ARM (OpenOCD):**
```bash
cd 09_hps_debug/quartus
make openocd
```
Wait until you see: `Listening on port 3333 for gdb connections`

**Project 08 — Nios II:**
```bash
cd 08_nios2_debug/quartus
make gdb-server
```
Wait until the server prints that it is listening on port 2345.

---

## Step 3 — Launch the VS Code debugger

1. Open the `cvsoc` repository root in VS Code.
2. Switch to the **Run and Debug** view (`Ctrl+Shift+D`).
3. Select the configuration from the dropdown:

| Configuration | GDB client | Project |
|---------------|------------|---------|
| `Toolchain: Debug HPS (Project 09)` | `arm-none-eabi-gdb` via wrapper | 09 — HPS |
| `Host: Debug HPS (Project 09)` | `gdb-multiarch` (host) | 09 — HPS (alternative) |
| `Toolchain: Debug Nios II (Project 08)` | `nios2-elf-gdb` via wrapper | 08 — Nios II |

4. Click the green **Play** button (or press `F5`).

VS Code automatically runs the `preLaunchTask` defined in `.vscode/tasks.json`, which starts `make openocd` or `make gdb-server` in a dedicated terminal panel and waits for the "Listening on port" message before connecting. Once the server is ready, VS Code spawns the GDB wrapper, connects to the server, loads the `.elf` into target memory, and halts at the first breakpoint (`main` for HPS, `set_led` for Nios II).

---

## What you can do once halted

*   Step over (`F10`) or step into (`F11`) functions.
*   Inspect local variables in the **Variables** pane.
*   Add `g_debug` / `debug_state` to the **Watch** pane to monitor hardware state.
*   View the call stack in the **Call Stack** pane.

When finished, click the red **Stop** button (`Shift+F5`) to disconnect, then press `Ctrl+C` in the GDB-server terminal.

---

## Troubleshooting

### "Unable to start debugging. Unexpected GDB output from command..."

VS Code cannot connect to the GDB server.
*   Ensure `make openocd` or `make gdb-server` is still running in a terminal and has printed its "Listening on port…" message.
*   Check that no other process is already using port 3333 / 2345.

### "miDebuggerPath is invalid" / wrapper script not found

VS Code cannot find the wrapper script.
*   Confirm the scripts exist and are executable:
    ```bash
    ls -l common/docker/*-gdb-wrapper.sh
    ```
*   If needed, restore permissions: `chmod +x common/docker/*-gdb-wrapper.sh`

### Docker container fails to start (wrapper)

*   Verify Docker is running: `docker info`
*   Confirm the image is built: `docker image inspect cvsoc/quartus:23.1`
*   If the image is missing, build it: `docker build -t cvsoc/quartus:23.1 common/docker/`

### Breakpoints aren't hitting (Nios II)

The Nios II Tiny core has one hardware breakpoint comparator, often consumed by the GDB server itself. Use software breakpoints (`break`) rather than hardware breakpoints (`hbreak`) for Nios II. The provided `launch.json` already does this.

### "gdb-multiarch cannot debug Nios II"

`gdb-multiarch` on Ubuntu lacks Nios II support. Use **`Toolchain: Debug Nios II (Project 08)`** instead — it routes through `nios2-elf-gdb` inside the Docker image.


