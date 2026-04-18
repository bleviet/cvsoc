# Visual Design Exploration — Launching Quartus & Platform Designer from Docker

> **Series:** cvsoc — Stepping into advanced FPGA development on the DE10-Nano  
> **Type:** Tutorial  
> **Difficulty:** Intermediate — you are comfortable with the CLI and now need to use visual tools

---

## What you will achieve

By the end of this tutorial, you will be able to launch the **Quartus Prime** and **Platform Designer** (Qsys) graphical interfaces directly from your Docker container. This allows you to inspect the RTL viewer, modify pin assignments visually, or explore the HPS component configuration without installing Quartus natively on your host.

---

## Prerequisites

| Requirement | Details |
|---|---|
| **Host OS** | Linux (Native) or Windows 11 with WSL2 (WSLg) |
| **Docker** | `raetro/quartus:23.1` image available locally |
| **X Server** | On Linux: standard X11. On WSL2: WSLg is built-in. |

---

## Step 1 — Prepare your Host for GUI Forwarding

Docker containers are isolated and do not have access to your screen by default. You must allow the container to connect to your host's X Window System.

Open a terminal on your **host machine** and run:

```bash
# Allow local Docker containers to connect to your X server
xhost +local:docker
```

*Expected output:* `non-network local connections being added to access control list`

---

## Step 2 — Launch the Quartus Prime GUI

To run the GUI, we must "mount" the host's X11 socket into the container and pass the `DISPLAY` environment variable so the application knows where to draw its windows.

Run the following command from the root of your `cvsoc` repository:

```bash
docker run --rm -it \
  --privileged \
  --net=host \
  -v /tmp/.X11-unix:/tmp/.X11-unix \
  -e DISPLAY=$DISPLAY \
  -v $(pwd):/work \
  raetro/quartus:23.1 \
  /opt/intelFPGA/quartus/bin/quartus
```

### Breaking down the command:
*   `-v /tmp/.X11-unix:/tmp/.X11-unix`: Shares the X11 communication socket.
*   `-e DISPLAY=$DISPLAY`: Tells the container to use your host's display.
*   `-v $(pwd):/work`: Maps your current project folder to `/work` inside the container.
*   `/opt/intelFPGA/quartus/bin/quartus`: The absolute path to the Quartus GUI binary.

### Opening your project:
1.  Once the Quartus window appears, go to **File > Open Project**.
2.  Navigate to `/work/04_nios2_led/quartus/` (or any other project folder).
3.  Select the **`.qpf`** file and click **Open**.

---

## Step 3 — Launch Platform Designer (Qsys)

Platform Designer is a separate tool that must be launched independently if you want to inspect the system interconnect.

Run the following command:

```bash
docker run --rm -it \
  --privileged \
  --net=host \
  -v /tmp/.X11-unix:/tmp/.X11-unix \
  -e DISPLAY=$DISPLAY \
  -v $(pwd):/work \
  raetro/quartus:23.1 \
  /opt/intelFPGA/quartus/sopc_builder/bin/qsys-edit
```

### Opening your system:
1.  In the Platform Designer window, go to **File > Open**.
2.  Navigate to `/work/04_nios2_led/qsys/`.
3.  Select the **`.qsys`** file (e.g., `nios2_system.qsys`) and click **Open**.

---

## Troubleshooting

### "D-Bus library appears to be incorrectly set up"
You may see this warning in your terminal after launching. **You can safely ignore this.** It occurs because the Docker container doesn't have a system-wide D-Bus daemon, but it does not affect the functionality of Quartus or Qsys.

### The window is completely blank or black
This usually happens if your host uses Wayland instead of X11. You can force X11 compatibility by adding `-e GDK_BACKEND=x11` to your `docker run` command.

### "Connection refused" or "Can't open display"
Ensure you ran `xhost +local:docker` on your host machine before starting the container. If you are on WSL2 and this still fails, ensure you can run a simple GUI app like `x11-apps` (e.g., `xclock`) inside WSL first.

---

## Summary

You can now move seamlessly between the **CLI for automation** and the **GUI for exploration**. 

*   Use the **CLI** (`make all`) for builds and CI/CD.
*   Use the **GUI** (`quartus` / `qsys-edit`) for RTL viewing, Chip Planner, and visual system integration.
