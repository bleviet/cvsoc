# How-to: Debug Nios II and ARM HPS with VS Code

> **Series:** cvsoc — Stepping into advanced FPGA development on the DE10-Nano  
> **Type:** How-to Guide  
> **Difficulty:** Intermediate  
> **Applies to:** Phase 5 (Projects 08 and 09)

---

This guide explains how to connect the Visual Studio Code (VS Code) graphical debugger to the Nios II and ARM Cortex-A9 embedded targets.

In Phase 5, you learned how to use the command-line GDB clients (`nios2-elf-gdb` and `arm-none-eabi-gdb`) running inside a Docker container. This guide uses a hybrid approach: the GDB server (which talks to the USB-Blaster) stays isolated in Docker, while a multi-architecture GDB client runs on your host machine to drive the VS Code UI.

---

## Prerequisites

Before starting, ensure you have:

1.  **Completed Phase 5:** You must be able to successfully compile and run either `08_nios2_debug` or `09_hps_debug` from the command line.
2.  **VS Code Installed:** Running on your host machine (Linux native or Windows via WSL Remote).
3.  **C/C++ Extension:** The official Microsoft `ms-vscode.cpptools` extension installed in VS Code.

---

## Step 1 — Install Host Dependencies

VS Code needs a GDB executable on your host machine (the environment where VS Code is running) to act as the client. Instead of installing the massive Intel FPGA toolchain natively just for GDB, we install a lightweight, generic GDB client that supports multiple architectures (including ARM and Nios II).

Run this command on your host machine (Linux native or inside your WSL2 terminal):

```bash
sudo apt-get update
sudo apt-get install gdb-multiarch
```

Verify the installation:

```bash
gdb-multiarch --version
```

---

## Step 2 — Configure VS Code (`launch.json`)

VS Code uses a `launch.json` file to define debug configurations.

1.  Open the `cvsoc` repository root in VS Code.
2.  Create a folder named `.vscode` if it does not exist.
3.  Create a file named `.vscode/launch.json` and paste the following configuration:

```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "Debug HPS (Project 09)",
            "type": "cppdbg",
            "request": "launch",
            "program": "${workspaceFolder}/09_hps_debug/software/app/hps_debug.elf",
            "cwd": "${workspaceFolder}/09_hps_debug/software/app",
            "MIMode": "gdb",
            "miDebuggerPath": "/usr/bin/gdb-multiarch",
            "miDebuggerServerAddress": "localhost:3333",
            "setupCommands": [
                {
                    "description": "Disable MMU and Caches",
                    "text": "monitor arm mcr 15 0 1 0 0 0",
                    "ignoreFailures": true
                },
                {
                    "description": "Load ELF into OCRAM",
                    "text": "load",
                    "ignoreFailures": false
                },
                {
                    "description": "Halt at main",
                    "text": "hbreak main",
                    "ignoreFailures": false
                }
            ]
        },
        {
            "name": "Debug Nios II (Project 08)",
            "type": "cppdbg",
            "request": "launch",
            "program": "${workspaceFolder}/08_nios2_debug/software/app/nios2_debug.elf",
            "cwd": "${workspaceFolder}/08_nios2_debug/software/app",
            "MIMode": "gdb",
            "miDebuggerPath": "/usr/bin/gdb-multiarch",
            "miDebuggerServerAddress": "localhost:2345",
            "setupCommands": [
                {
                    "description": "Load ELF into On-Chip RAM",
                    "text": "load",
                    "ignoreFailures": false
                },
                {
                    "description": "Set breakpoint at set_led",
                    "text": "break set_led",
                    "ignoreFailures": false
                }
            ]
        }
    ]
}
```

### Understanding the configuration

*   `miDebuggerPath`: Points to the `gdb-multiarch` binary you installed on your host.
*   `miDebuggerServerAddress`: Tells VS Code to connect to the Docker container's exposed ports (`3333` for OpenOCD, `2345` for nios2-gdb-server).
*   `setupCommands`: These execute automatically when the debugger attaches. They mirror the initialization scripts (`hps_debug.gdb` and `nios2_debug.gdb`) used in the command-line tutorial.

---

## Step 3 — Start the Debug Session

Because VS Code is acting only as the *client*, you must start the GDB *server* manually before launching the debugger.

### Example: Debugging the ARM HPS (Project 09)

1.  **Program the FPGA:** (If not already done)
    ```bash
    cd 09_hps_debug/quartus
    make program-sof
    ```
2.  **Start OpenOCD (The Server):**
    In an integrated VS Code terminal, start the Dockerized OpenOCD server:
    ```bash
    make openocd
    ```
    *Leave this terminal running. Wait until you see `Listening on port 3333 for gdb connections`.*
3.  **Launch VS Code Debugger:**
    *   Switch to the **Run and Debug** view in the VS Code sidebar (`Ctrl+Shift+D`).
    *   Select **Debug HPS (Project 09)** from the dropdown menu.
    *   Click the green **Play** button (or press `F5`).

VS Code will connect to the OpenOCD server, load the `.elf` binary into the HPS OCRAM, and immediately halt execution at the `main()` function.

You can now use the VS Code interface to:
*   Step over (`F10`) or step into (`F11`) functions.
*   Inspect local variables in the **Variables** pane.
*   Add `g_debug` to the **Watch** pane to monitor the hardware state.
*   View the ARM exception call stack in the **Call Stack** pane.

When you are finished, click the red **Stop** button (`Shift+F5`) in VS Code to disconnect, then press `Ctrl+C` in the terminal to shut down OpenOCD.

---

## Troubleshooting

### "Unable to start debugging. Unexpected GDB output from command..."

This usually means VS Code cannot connect to the server.
*   Ensure the `make openocd` or `make gdb-server` command is running in a terminal.
*   Verify the server has finished booting (look for the "Listening on port..." message).
*   If you are using WSL2, ensure you are running VS Code in the WSL context (the bottom-left corner of VS Code should say "WSL: Ubuntu").

### "miDebuggerPath is invalid"

VS Code cannot find `gdb-multiarch`.
*   Verify you ran `sudo apt-get install gdb-multiarch` in the same environment where the VS Code server is running.
*   Run `which gdb-multiarch` to confirm the path is `/usr/bin/gdb-multiarch`.

### Breakpoints aren't hitting (Nios II)

The Nios II Tiny core only has one hardware breakpoint comparator, which is often consumed by the GDB server itself. Ensure your `setupCommands` in `launch.json` use software breakpoints (`break`) rather than hardware breakpoints (`hbreak`) for Nios II projects. The provided `launch.json` template already handles this correctly.
