# Phase 5 Tutorial — Software Debugging with GDB

> **Series:** cvsoc — Stepping into advanced FPGA development on the DE10-Nano  
> **Phase:** 5 of 8  
> **Difficulty:** Intermediate-Advanced — you have completed phases 0–4 and are comfortable with Platform Designer, the Quartus compile flow, ARM bare-metal programming, and hardware interrupt handling

---

## What you will build

This phase covers two standalone projects that each add **source-level GDB debugging** to the LED and interrupt demos from Phase 4. The hardware designs are reused unchanged; the firmware is recompiled with `-O0 -g3` (no optimisation, maximum debug information) so that every variable, struct field, and function call is visible to GDB.

**Project 08 — Nios II debugging via `nios2-gdb-server`**

The Nios II debug unit exposes a JTAG bridge that `nios2-gdb-server` converts into a GDB remote-serial-protocol endpoint. `nios2-elf-gdb` connects over TCP, loads the ELF, and gives you a full interactive debugging session — breakpoints, watchpoints, register inspection, memory reads, backtraces.

```
           ┌─────────────────────────────────────────────────────────────────┐
           │              nios2_system (Platform Designer)                    │
           │                                                                   │
FPGA_CLK1_50 ──► clk_bridge ──► Nios II/e CPU                               │
                              ├──► on-chip RAM (32 KB)   0x00000000  ← ELF   │
                              ├──► JTAG UART             0x00010100  IRQ 0   │
                              ├──► LED PIO (8-bit)       0x00010010          │
                              └──► button_pio (2-bit)    0x00010020  IRQ 1   │
           └─────────────────────────────────────────────────────────────────┘
                        │
             JTAG USB ──┤──► nios2-gdb-server :2345
                        │             │
                        │    nios2-elf-gdb ──► (gdb) prompt
```

**Project 09 — ARM HPS debugging via OpenOCD**

Intel's OpenOCD build uses the `aji_client` interface (the same JTAG stack as Quartus) to connect to the ARM DAP in the Cyclone V HPS. OpenOCD exposes a GDB server on port 3333. `arm-none-eabi-gdb` connects, loads the ELF into OCRAM, and gives you hardware breakpoints, watchpoints, and direct register access.

```
           ┌───────────────────────────────────────────────────────────────────┐
           │            Cyclone V — HPS + FPGA Fabric                          │
           │                                                                     │
FPGA_CLK1_50 ──► HPS Cortex-A9 ──► GIC ──► irq_c_handler() (bare-metal)      │
                    │  └── OCRAM (64 KB @ 0xFFFF0000) ◄── ELF loaded via GDB  │
                    │  └── LW H2F bridge (0xFF200000)                          │
                    │         ├── LED PIO  (0xFF200000)                         │
                    │         └── button_pio (0xFF201000)                       │
                    │                          ↑ KEY[1:0]                       │
                    └── ARM DAP ──► OpenOCD :3333 ──► arm-none-eabi-gdb       │
           └───────────────────────────────────────────────────────────────────┘
```

Every step — bitstream programming, firmware build, GDB server startup, and GDB client launch — is driven from the command line inside Docker. No GUI interaction is required at any point.

---

## Prerequisites

| Requirement | Details |
|---|---|
| **Docker** | `cvsoc/quartus:23.1` image available locally |
| **Repository** | `git clone` of `bleviet/cvsoc`; phases 0–4 already working |
| **Phase 4** | Interrupt handling for Nios II (project 06) and HPS (project 07) understood |
| **SOF files** | `06_nios2_interrupts/quartus/de10_nano.sof` and `07_hps_interrupts/quartus/de10_nano.sof` built and available |
| **Board** | Terasic DE10-Nano (Cyclone V `5CSEBA6U23I7`) powered and connected via USB |
| **JTAG cable** | USB-Blaster II (built into the DE10-Nano board) |

Verify the Docker image and SOF files before continuing:

```bash
docker images | grep cvsoc/quartus
# Expected: cvsoc/quartus   23.1   ...

ls 06_nios2_interrupts/quartus/de10_nano.sof
ls 07_hps_interrupts/quartus/de10_nano.sof
# Both files must exist. Run 'make compile' in the respective quartus/ directory
# if either is missing.
```

---

## Concepts in 5 minutes

Before touching any file, read these ideas. They explain *why* each tool is needed and *how* the pieces fit together.

### GDB remote debugging

GDB has two modes:

- **Local debugging** — GDB runs on the same machine as the program. Not possible for embedded targets that have no OS.
- **Remote debugging** — GDB (the *client*) runs on your development machine and communicates with a *GDB server* running on or near the target over TCP.

Both projects in this phase use remote debugging. The GDB server is a bridge between JTAG hardware and the GDB remote serial protocol (RSP).

```
GDB client                                 Target
(nios2-elf-gdb or arm-none-eabi-gdb)       (FPGA / HPS)
   │                                            │
   │←── GDB Remote Serial Protocol (TCP) ──────┤
   │                                     GDB server
   │                                     (nios2-gdb-server / OpenOCD)
   │                                            │
   │                                     JTAG USB
```

### Breakpoints: software vs. hardware

| Type | Mechanism | Use when |
|------|-----------|----------|
| **Software** (`break`) | Overwrites a RAM instruction with a trap/BKPT instruction | Code is in writable RAM — the normal case for OCRAM |
| **Hardware** (`hbreak`) | Uses a CPU comparator register; does NOT modify memory | Code is in ROM, Flash, or you need to avoid modifying code |

> **Nios II Tiny (Economy) core** has only one hardware breakpoint trigger, which it also uses for the gdb-server's internal operation. Use `break` (software) for all Nios II breakpoints in this phase. The Cortex-A9 has 6 hardware comparators — `hbreak` works well there.

### Watchpoints

A watchpoint halts the CPU whenever a specific memory location is *written* (write watchpoint) or *read* (read watchpoint). The Cortex-A9 has 4 hardware watchpoint registers, so `watch g_debug.led_pattern` fires with no overhead. The Nios II Tiny core does not support hardware watchpoints.

### Why `-O0 -g3`?

Production firmware is often compiled with `-Os` (size optimisation) or `-O2` (speed optimisation). These flags allow the compiler to:
- Remove "unused" variables (they may exist only in registers, invisible to GDB).
- Inline small functions (no frame to break into).
- Reorder instructions (step commands skip lines).

`-O0` disables all optimisation. `-g3` emits the maximum amount of DWARF debug information, including macro definitions. Together they ensure that every variable, struct field, and function call is exactly where GDB expects it.

---

## Part 1 — Nios II debugging (project 08)

### Build the debug firmware

The hardware is identical to project 06. Only the firmware changes.

```bash
cd 08_nios2_debug/quartus

# Build BSP + application (inside Docker)
docker run --rm \
  -v "$(pwd)/../..:/work" \
  -v "$(pwd)/../../common/docker/uname_shim.sh:/usr/local/bin/uname:ro" \
  cvsoc/quartus:23.1 \
  bash -c "cd /work/08_nios2_debug/quartus && make bsp app"
```

The output is `software/app/nios2_debug.elf`. Verify the size:

```
   text    data     bss     dec     hex filename
   3036      16     272    3324     cfc nios2_debug.elf
```

> If the BSP is missing, run `make all` first to regenerate it from the sopcinfo.  
> Or copy the pre-existing BSP from project 06 (same hardware):
> ```bash
> cp -r 06_nios2_interrupts/software/bsp 08_nios2_debug/software/bsp
> docker run --rm -v "$(pwd)/../..:/work" cvsoc/quartus:23.1 \
>   bash -c "cd /work/08_nios2_debug/software/app && make"
> ```

### What the firmware does

`main.c` groups all observable state in a single struct:

```c
typedef struct {
    uint8_t  led_pattern;   /* current LED output value        */
    uint32_t step_count;    /* main-loop iterations since reset */
    uint32_t irq_count;     /* button presses (ISR count)       */
    uint8_t  last_edges;    /* edge bits from last button press  */
} debug_state_t;

debug_state_t debug_state;  /* non-static — GDB can find by name */
```

Key functions are marked `__attribute__((noinline))` to guarantee they appear as distinct frames in the backtrace:

| Function | Role in the debug demo |
|---|---|
| `set_led(pattern)` | Primary breakpoint target — fires on every LED change |
| `delay_ms(ms)` | Shows a simple loop in `next`/`step` commands |
| `process_button(edges)` | ISR helper — demonstrates backtrace from interrupt context |

### Program the FPGA (project 06 SOF)

Project 08 reuses the `06_nios2_interrupts` bitstream. Program it once, then leave the FPGA loaded.

**On Windows/WSL2 with usbipd:**
```bash
# Detach USB from WSL so Windows can access it
usbipd.exe detach --busid 2-4

# Copy SOF to a Windows-accessible path
cp 06_nios2_interrupts/quartus/de10_nano.sof /mnt/c/Windows/Temp/de10_nano.sof

# Program via Windows Programmer (Quartus must be installed on Windows)
/mnt/c/intelFPGA_lite/23.1std/qprogrammer/bin64/quartus_pgm.exe \
  -m jtag -o "p;C:\Windows\Temp\de10_nano.sof@2"

# Re-attach USB to WSL for JTAG tools
usbipd.exe attach --wsl --busid 2-4
```

**On native Linux:**
```bash
cd 06_nios2_interrupts/quartus
make program-sof
```

Verify the JTAG chain after programming:
```bash
docker run --rm --privileged \
  -v "$(pwd)/../../common/docker/uname_shim.sh:/usr/local/bin/uname:ro" \
  cvsoc/quartus:23.1 \
  bash -c "/opt/intelFPGA/quartus/bin/jtagd && sleep 2 && \
           /opt/intelFPGA/quartus/bin/jtagconfig"
# Expected:
# 1) DE-SoC [1-1]
#   4BA00477   SOCVHPS
#   02D020DD   5CSEBA6(.|ES)/5CSEMA6/..
```

### Start the GDB server (Terminal 1)

```bash
cd 08_nios2_debug/quartus
make gdb-server
```

Under the hood this runs:
```bash
docker run --rm -it --privileged \
  --name nios2-gdb-server \
  -p 2345:2345 \
  -v "$(repo_root):/work" \
  -v ".../uname_shim.sh:/usr/local/bin/uname:ro" \
  cvsoc/quartus:23.1 \
  bash -c '/opt/intelFPGA/quartus/bin/jtagd && sleep 2 && \
    nios2-gdb-server --tcpport 2345 --tcppersist --tcpdebug'
```

Wait until you see:
```
Using cable "DE-SoC [1-1]", device 2, instance 0x00
Processor is already paused
Listening on port 2345 for connection from GDB:
```

> **`--tcppersist`** keeps the server running after GDB disconnects, so you can reconnect without restarting it.

### Launch the GDB client (Terminal 2)

```bash
cd 08_nios2_debug/quartus
make gdb
```

GDB starts, executes `scripts/nios2_debug.gdb` automatically, loads the ELF, sets a breakpoint at `set_led()`, and runs. You will see the Nios II halt at `set_led()` within a few seconds:

```
Breakpoint 1, set_led (pattern=1) at main.c:74
74          IOWR_ALTERA_AVALON_PIO_DATA(LED_PIO_BASE, pattern);
(gdb)
```

### Debugging walkthrough

#### 1. Inspect the debug struct

```
(gdb) print debug_state
$1 = {led_pattern = 0, step_count = 0, irq_count = 0, last_edges = 0}
```

`led_pattern` is 0 because we are stopped *at* the write instruction, before it executes. `step_count` is 0 — this is the very first call to `set_led()`.

#### 2. Step through the LED write

```
(gdb) next
75          debug_state.led_pattern = pattern;
(gdb) next
set_led returns
(gdb) print debug_state.led_pattern
$2 = 1 '\001'
```

The LED register has been written; `debug_state.led_pattern` reflects the new value.

#### 3. Read the LED PIO hardware register directly

```
(gdb) x/1xw 0x10010
0x10010:  0x00000001
```

This reads the Avalon LED PIO data register straight from the Nios II address space — no HAL calls needed.

#### 4. Continue and observe the pattern advancing

```
(gdb) continue
Breakpoint 1, set_led (pattern=3) at main.c:74
(gdb) print debug_state.step_count
$3 = 1
```

Each `continue` runs the main loop once more before hitting `set_led()` again.

#### 5. Set a breakpoint in the ISR

Press KEY[0] or KEY[1] on the board *after* running this command:

```
(gdb) break process_button
Breakpoint 2 at 0x314: file main.c, line 98.
(gdb) continue

# Press KEY[0] now — GDB halts inside the ISR:
Breakpoint 2, process_button (edges=1) at main.c:98
98          debug_state.last_edges = edges;
```

#### 6. Backtrace from the ISR

```
(gdb) bt
#0  process_button (edges=1) at main.c:98
#1  0x000003b8 in button_isr (context=0x0) at main.c:119
#2  <signal handler called>
#3  0x0000035c in delay_ms (ms=300) at main.c:82
#4  0x000003f0 in main () at main.c:151
```

The backtrace shows the full call chain from `main()` through the interrupt to the ISR.

#### 7. Inspect button PIO registers

```
(gdb) inspect-button-pio
=== Button PIO registers (0x00010020) ===
DATA      (0x00010020):  0x00000003   ← both buttons released (active-LOW)
IRQ_MASK  (0x00010028):  0x00000003   ← both buttons armed
EDGE_CAP  (0x0001002C):  0x00000000   ← ISR already cleared this
```

#### 8. Useful quick-reference commands

| Command | Effect |
|---|---|
| `print debug_state` | Print all fields of the struct |
| `x/1xw 0x10010` | Read LED PIO DATA register |
| `inspect-led-pio` | Custom helper: same as above with label |
| `inspect-button-pio` | Custom helper: all 4 button PIO registers |
| `bt` | Print call stack (backtrace) |
| `step` | Step one source line (enters called functions) |
| `next` | Step one source line (skips over called functions) |
| `finish` | Run until the current function returns |
| `info registers` | Print all CPU registers |
| `continue` | Resume until next breakpoint |
| `disconnect` | Detach from the GDB server (target keeps running) |

---

## Part 2 — ARM HPS debugging (project 09)

### Install the ARM toolchain

Project 09 requires `arm-linux-gnueabihf-gcc` and `arm-none-eabi-gdb`. Install them inside the Docker container (they are not in the base image):

```bash
cd 09_hps_debug/quartus
docker run --rm \
  -v "$(pwd)/../..:/work" \
  cvsoc/quartus:23.1 \
  bash -c "cd /work/09_hps_debug/quartus && make setup"
```

`make setup` runs this once inside the container:
```bash
echo "deb http://snapshot.debian.org/archive/debian/20220622T000000Z stretch main" \
  > /etc/apt/sources.list
apt-get update -o Acquire::Check-Valid-Until=false -qq
apt-get install -y gcc-arm-linux-gnueabihf binutils-arm-linux-gnueabihf gdb-arm-none-eabi
```

> **Note:** The `make gdb` target installs `gdb-arm-none-eabi` inline on every run, so the above is optional. `make setup` is useful for pre-caching the toolchain before an offline session.

### Build the debug firmware

```bash
docker run --rm \
  -v "$(pwd)/../..:/work" \
  cvsoc/quartus:23.1 \
  bash -c "
    echo 'deb http://snapshot.debian.org/archive/debian/20220622T000000Z stretch main' \
      > /etc/apt/sources.list
    apt-get update -o Acquire::Check-Valid-Until=false -qq 2>/dev/null
    apt-get install -y -o Acquire::Check-Valid-Until=false \
      gcc-arm-linux-gnueabihf binutils-arm-linux-gnueabihf -qq 2>/dev/null
    cd /work/09_hps_debug/software/app && make"
```

The output is `software/app/hps_debug.elf` (≈1.2 KB) and `hps_debug.bin`. Verify:

```
   text    data     bss     dec     hex filename
   1054     168       0    1222     4c6 hps_debug.elf
```

### What the firmware does

`main.c` uses a `debug_info_t` struct to group all observable state:

```c
typedef struct {
    uint8_t  led_pattern;    /* current LED output                          */
    uint32_t step_count;     /* main-loop iterations                         */
    uint32_t irq_count;      /* total interrupts handled                     */
    uint32_t last_irq_id;    /* GIC IAR value of last interrupt (ID 72 = button) */
    uint8_t  last_edges;     /* button_pio edge bits of last press            */
} debug_info_t;

volatile debug_info_t g_debug;   /* non-static — GDB finds it by name */
```

`last_irq_id` captures the GIC Interrupt Acknowledge Register (GICC_IAR) so you can confirm the interrupt source at the GDB prompt without any printf output.

The startup sequence calls five functions in order — a clean, step-able boot path:

```
_start (startup.S)
  → set up exception vectors
  → initialise stacks (IRQ @ 0xFFFFFFFC, SVC @ 0xFFFFFDFC)
  → call main()
      → wdt_init()          ← disable watchdogs
      → hps_bridge_init()   ← enable LW H2F bridge
      → gic_init()          ← configure GIC distributor + CPU interface
      → button_pio_init()   ← arm edge-capture IRQ
      → cpsie i             ← enable IRQs globally
      → while(1) loop       ← advance LED pattern + kick watchdogs
```

### Program the FPGA (project 07 SOF)

Project 09 reuses the `07_hps_interrupts` bitstream.

```bash
# On WSL2 with usbipd:
cp 07_hps_interrupts/quartus/de10_nano.sof /mnt/c/Windows/Temp/de10_nano_hps.sof
usbipd.exe detach --busid 2-4
/mnt/c/intelFPGA_lite/23.1std/qprogrammer/bin64/quartus_pgm.exe \
  -m jtag -o "p;C:\Windows\Temp\de10_nano_hps.sof@2"
usbipd.exe attach --wsl --busid 2-4
```

### Start OpenOCD (Terminal 1)

```bash
cd 09_hps_debug/quartus
make openocd
```

OpenOCD starts, enumerates the JTAG chain, identifies the ARM DAP and the Cyclone V FPGA, halts the Cortex-A9, and waits for a GDB connection:

```
Info : fpgasoc.cpu.0: hardware has 6 breakpoints, 4 watchpoints
Info : starting gdb server for fpgasoc.cpu.0 on 3333
Info : Listening on port 3333 for gdb connections
target halted in ARM state due to debug-request, current mode: Supervisor
```

> The CPU is halted at whatever it was executing before. **Do not send `resume` yet** — GDB will load the ELF and set the program counter.

### Launch the GDB client (Terminal 2)

```bash
cd 09_hps_debug/quartus
make gdb
```

`make gdb` runs a Docker container with `--network host` so it can reach `localhost:3333` in the OpenOCD container. GDB executes `scripts/hps_debug.gdb` automatically:

1. Connects to `localhost:3333`
2. Disables MMU and D-cache (so memory reads reflect raw register values)
3. Loads the ELF — writes `.startup` + `.text` + `.data` to OCRAM and sets `$pc` to `_start` (0xFFFF0020)
4. Sets a hardware breakpoint at `main()`
5. Sets a watchpoint on `g_debug.led_pattern`
6. Resumes execution

You will see GDB halt at the entry to `main()` within a few seconds:

```
Breakpoint 1, main () at main.c:237
237         wdt_init();
(gdb)
```

### Debugging walkthrough

#### 1. Inspect the debug struct at boot

```
(gdb) print g_debug
$1 = {led_pattern = 1, pad = "\000\000", step_count = 0, irq_count = 0,
      last_irq_id = 0, last_edges = 0, pad2 = "\000\000"}
```

All counters are zero. `led_pattern = 1` is the compile-time initial value.

#### 2. Step through the initialisation sequence

```
(gdb) next       ← runs wdt_init()
238         hps_bridge_init();
(gdb) next       ← runs hps_bridge_init()
239         gic_init();
(gdb) next       ← runs gic_init()
240         button_pio_init();
```

Each `next` executes one source line, stepping *over* function calls. To trace *into* `gic_init()`, use `step` instead.

#### 3. Verify GIC configuration after `gic_init()`

After `next` has stepped over `gic_init()`, confirm the GIC is armed:

```
(gdb) inspect-gicd
=== GIC Distributor (0xFFFED000) ===
GICD_CTLR       (0xFFFED000):  0x00000001   ← forwarding enabled
GICD_ISENABLER1 (0xFFFED104):  0x00000100   ← bit 8 = ID 72 enabled
                                                  (72 mod 32 = 8)
```

If `GICD_CTLR` is 0, `gic_init()` has not run yet. If `GICD_ISENABLER1` bit 8 is 0, the interrupt for the button PIO is not armed.

#### 4. Continue to the main loop and watch the LED pattern

```
(gdb) continue
# The watchpoint fires as soon as the main loop writes g_debug.led_pattern:
Hardware watchpoint 2: g_debug.led_pattern

Old value = 1 '\001'
New value = 3 '\003'
main () at main.c:255
255         LED_PIO_DATA = g_debug.led_pattern;
```

The watchpoint reports the old and new values. You can see the LED pattern advancing through the sequence `0x01 → 0x03 → 0x07 → ...` without setting explicit breakpoints.

#### 5. Break on a button press

```
(gdb) hbreak irq_c_handler
Breakpoint 3 at 0xffff0168: file main.c, line 133.
(gdb) continue

# Press KEY[0] on the board — GDB halts in the IRQ handler:
Breakpoint 3, irq_c_handler () at main.c:135
135         uint32_t iar = GICC_IAR;
```

#### 6. Confirm the interrupt source

```
(gdb) next
(gdb) print g_debug.last_irq_id
$2 = 72
```

GIC interrupt ID 72 is SPI[40], which maps to `f2h_irq0[0]` — the button PIO IRQ line. This confirms the full chain: KEY[0] → button_pio → FPGA-to-HPS interrupt → GIC → Cortex-A9 IRQ mode → `irq_c_handler()`.

#### 7. Inspect GIC CPU Interface during the IRQ

While halted inside `irq_c_handler()`, before calling `GICC_EOIR`:

```
(gdb) inspect-gicc
=== GIC CPU Interface (0xFFFEC100) ===
GICC_CTLR (0xFFFEC100):  0x00000001   ← CPU interface enabled
GICC_PMR  (0xFFFEC104):  0x000000f0   ← priority mask (all pass)
GICC_IAR  (0xFFFEC10C):  0x00000048   ← 0x48 = 72 (interrupt pending)
```

`GICC_IAR` shows `0x48` (72 decimal). Reading it acknowledged the interrupt. Writing this same value to `GICC_EOIR` will signal End-of-Interrupt to the GIC.

#### 8. Continue past the ISR and observe the updated struct

```
(gdb) finish
(gdb) print g_debug
$3 = {led_pattern = 3, pad = "\000\000", step_count = 4, irq_count = 1,
      last_irq_id = 72, last_edges = 1, pad2 = "\000\000"}
```

`irq_count` is now 1, `last_irq_id` confirms ID 72, and `last_edges` shows bit 0 set (KEY[0] pressed).

#### 9. Read the LW H2F bridge peripherals directly

```
(gdb) inspect-led-pio
=== LED PIO DATA (0xFF200000) ===
0xff200000:  0x00000003

(gdb) inspect-button-pio
=== Button PIO (LW H2F 0xFF201000) ===
DATA      (0xFF201000):  0x00000003   ← both buttons released
IRQ_MASK  (0xFF201008):  0x00000003   ← both armed
EDGE_CAP  (0xFF20100C):  0x00000000   ← ISR cleared this
```

These are raw reads through the LW H2F bridge at ARM physical addresses — no drivers, no MMIO wrappers.

#### 10. Quick-reference commands

| Command | Effect |
|---|---|
| `print g_debug` | Print all fields of the debug struct |
| `watch g_debug.led_pattern` | Break on every LED pattern change |
| `hbreak irq_c_handler` | Hardware breakpoint — doesn't modify OCRAM |
| `inspect-gicd` | GIC Distributor enable + priority registers |
| `inspect-gicc` | GIC CPU Interface status + IAR |
| `inspect-led-pio` | Read LED PIO DATA at 0xFF200000 |
| `inspect-button-pio` | Read all 4 button PIO registers |
| `si` / `stepi` | Single instruction step (assembly level) |
| `x/4xw 0xFFFED000` | Dump raw GIC Distributor registers |
| `info registers` | All ARM core registers (r0–r15, cpsr) |
| `bt` | Backtrace |
| `disconnect` | Detach from OpenOCD (target keeps running) |

---

## Understanding the debug build

### Why the struct pattern?

Grouping state in a struct has two practical benefits for GDB debugging:

1. **Single-command inspection:** `print g_debug` dumps every observable field at once, without running a custom script.
2. **Watchpoint on a field:** `watch g_debug.led_pattern` monitors exactly the field you care about, not a loose global.

Compare the two approaches:
```c
/* Hard to inspect — scattered globals */
volatile uint8_t  g_led_pattern;
volatile uint32_t g_step_count;
volatile uint32_t g_irq_count;

/* Easy to inspect — single struct */
volatile debug_info_t g_debug;   /* (gdb) print g_debug */
```

### Why `__attribute__((noinline))`?

Without this attribute, an optimising compiler may inline `set_led()` into its one caller. When inlined, the function no longer exists as a symbol — `break set_led` fails with "no function set_led". The `noinline` attribute guarantees the function always has its own stack frame and a stable address.

This is only needed for `-O1` and above. At `-O0` (no optimisation), no inlining happens. The attribute is still a good habit because it documents your intent.

### The `volatile` keyword and GDB

`volatile` tells the compiler: *"do not cache this variable in a register; read it from memory every time."* Without `volatile`, GDB may show a stale register-cached value, or the compiler may optimize away a variable entirely.

In our firmware, `g_debug` is declared `volatile debug_info_t`. This ensures that:
- Every read in the C code goes through memory (so GDB's `print` shows the live value).
- Every write is committed to memory immediately (so GDB watchpoints trigger correctly).

---

## Troubleshooting

### `nios2-gdb-server`: "Resetting and pausing target processor: FAILED"

The `--reset-target` flag requires a JTAG-accessible reset line that this system design does not connect. Remove `--reset-target` from the invocation (the project Makefile already omits it). The server will instead pause the currently running CPU, which is equivalent for our purposes.

### `nios2-elf-gdb`: "Could not insert hardware watchpoint"

The Nios II Tiny (Economy) core has only one hardware comparator register. It cannot support both a hardware breakpoint and a hardware watchpoint simultaneously. Use software breakpoints (`break`, not `hbreak`) and reserve the hardware comparator for watchpoints — or use neither, and rely on software-only debugging:
```
(gdb) break set_led           ← software breakpoint (modifies RAM)
(gdb) break process_button    ← second software breakpoint — works fine
```

### OpenOCD: "JTAG server reports that it has no hardware cable"

A stale `jtagd` process from a previous Docker container is still holding the JTAG lock. Remove all stopped containers and reconnect the USB cable:
```bash
docker rm -f $(docker ps -aq)
usbipd.exe detach --busid 2-4
usbipd.exe attach --wsl --busid 2-4
sleep 2
```

### GDB: `localhost:3333: Connection timed out`

OpenOCD has not yet opened port 3333. Wait for the line `Listening on port 3333 for gdb connections` in the OpenOCD terminal before launching GDB.

### HPS firmware runs but LEDs don't change

The LW H2F bridge must be released from reset before the LED PIO is accessible. The firmware's `hps_bridge_init()` handles this. If you are debugging a custom firmware that skips `hps_bridge_init()`, run it manually at the GDB prompt:
```
(gdb) set *(unsigned int*)0xFF800000 = 0x11   ← L3_REMAP: enable LW H2F + OCRAM
(gdb) set *(unsigned int*)0xFFD0501C = 0x07   ← BRGMODRST: assert all bridges
(gdb) set *(unsigned int*)0xFFD0501C = 0x00   ← BRGMODRST: release all bridges
```

---

## What you have learned

| Concept | Where demonstrated |
|---|---|
| GDB remote serial protocol (TCP bridge) | Both projects |
| Software breakpoints in OCRAM | `break set_led` in project 08 |
| Hardware breakpoints (Cortex-A9) | `hbreak main`, `hbreak irq_c_handler` in project 09 |
| Hardware watchpoints | `watch g_debug.led_pattern` in project 09 |
| Struct inspection with `print` | `print debug_state`, `print g_debug` |
| Raw register reads with `x/xw` | LED PIO, button PIO, GIC registers |
| Backtrace from an interrupt handler | `bt` inside `button_isr` / `irq_c_handler` |
| GIC register map and acknowledgement protocol | `inspect-gicd`, `inspect-gicc` |
| Stepping through ARM assembly boot code | `si` (stepi) through `startup.S` |
| Volatile state and `-O0 -g3` | `debug_state_t`, `debug_info_t` |

---

## Next steps

- **Phase 6 — Embedded Linux:** Boot Buildroot Linux on the HPS and control FPGA LEDs from user-space via the UIO driver framework. See `docs/tutorial_phase6_embedded_linux.md`.
- **Advanced GDB scripting:** Extend `nios2_debug.gdb` and `hps_debug.gdb` with `define` commands and automatic logging of `g_debug` on every watchpoint fire.
- **Interrupt latency measurement:** Use a GPIO toggle inside the ISR and a logic analyser to measure the round-trip time from button press to ISR entry — compare with GDB step counts.
