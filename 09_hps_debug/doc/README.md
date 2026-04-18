# 09 — HPS ARM Software Debugging with GDB

Extends project 07 with **interactive GDB debugging** of the ARM Cortex-A9
HPS (Hard Processor System).  The same hardware design is reused; the firmware
is recompiled with `-O0 -g3`.  OpenOCD provides the GDB server over the
Intel `aji_client` JTAG interface.

## Architecture

```
           ┌──────────────────────────────────────────────────────────────┐
           │                   hps_system (Platform Designer)              │
           │                                                                │
FPGA_CLK1_50 ──► HPS Cortex-A9 (single core, bare-metal)                  │
                    │  ├── GIC (PPI/SPI interrupts)                        │
                    │  ├── OCRAM (64 KB @ 0xFFFF0000) ◄── ELF loaded      │
                    │  └── HPS-to-FPGA LW bridge                           │
                    │           ├── LED PIO (8-bit @ 0xFF200000)           │
                    │           └── button_pio (2-bit @ 0xFF201000)        │
                    │                    ▲  IRQ72 (f2h_irq0[0])            │
                    │           KEY[1:0] ┘                                  │
                    └── JTAG DAP ──► OpenOCD (port 3333) ──► GDB client
           └──────────────────────────────────────────────────────────────┘
```

### HPS memory map

| Region                       | Address range                | Notes                      |
|------------------------------|------------------------------|----------------------------|
| OCRAM (code + data)          | `0xFFFF0000` – `0xFFFFFFFF`  | 64 KB; execution target    |
| LW H2F bridge                | `0xFF200000` – `0xFF3FFFFF`  | 2 MB; Avalon peripherals   |
| LED PIO data                 | `0xFF200000`                 |                            |
| button_pio data              | `0xFF201000`                 |                            |
| GIC Distributor              | `0xFFFED000`                 |                            |
| GIC CPU Interface            | `0xFFFEC100`                 |                            |
| IRQ stack (descending)       | `0xFFFFFFFC` – `0xFFFFFDFC`  | 512 B; IRQ mode            |
| SVC stack (descending)       | `0xFFFFFDFC` – `0xFFFFFBFC`  | 512 B; supervisor mode     |

### GIC interrupt wiring

| GIC ID | Type  | Source                    |
|--------|-------|---------------------------|
| 72     | SPI   | `f2h_irq0[0]` = button_pio |

## Directory structure

```
09_hps_debug/
├── doc/
│   └── README.md                  ← this file
├── hdl/
│   └── de10_nano_top.vhd          ← identical to 07_hps_interrupts
├── qsys/
│   └── hps_system.tcl             ← identical to 07_hps_interrupts
├── quartus/
│   ├── Makefile                   ← adds openocd and gdb targets
│   ├── de10_nano_project.tcl
│   ├── de10_nano_pin_assignments.tcl
│   └── de10_nano.sdc
├── scripts/
│   ├── de10_nano_hps.cfg          ← OpenOCD config (aji_client interface)
│   ├── patch_oct.py               ← DDR3 OCT workaround
│   └── hps_debug.gdb              ← GDB init script
└── software/
    ├── app/
    │   ├── Makefile               ← -O0 -g3; ELF = hps_debug.elf
    │   ├── startup.S              ← ARMv7 exception vectors + stack setup
    │   ├── linker.ld              ← OCRAM @ 0xFFFF0000
    │   └── main.c                 ← g_debug struct, __attribute__((noinline))
```

## Building

```bash
docker run --rm \
  -v /path/to/cvsoc:/work \
  raetro/quartus:23.1 \
  bash -c "cd /work/09_hps_debug/quartus && make all"
```

| Step | Target    | Tool                                | Output                       |
|------|-----------|-------------------------------------|------------------------------|
| 1    | `qsys`    | `qsys-script` + `qsys-generate`     | `qsys/hps_system/`           |
| 2    | `project` | `quartus_sh -t`                     | `.qpf`, `.qsf`               |
| 3    | `compile` | two-pass `quartus_map/fit/asm/sta`  | `.sof` bitstream             |
| 4    | `app`     | `arm-linux-gnueabihf-gcc -O0 -g3`  | `software/app/hps_debug.elf` |

## GDB debugging workflow

### Start OpenOCD (Terminal 1)

```bash
docker run --rm \
  -v /path/to/cvsoc:/work \
  --device /dev/bus/usb \
  raetro/quartus:23.1 \
  bash -c "cd /work/09_hps_debug/quartus && make openocd"
```

This programs the FPGA, loads the binary into OCRAM via OpenOCD, and leaves
OpenOCD running with the GDB server active on port 3333.

### Launch GDB client (Terminal 2)

```bash
docker run --rm -it \
  -v /path/to/cvsoc:/work \
  --network host \
  raetro/quartus:23.1 \
  bash -c "cd /work/09_hps_debug/quartus && make gdb"
```

GDB connects to `localhost:3333`, loads symbol information, sets a hardware
breakpoint at `main()` and a watchpoint on `g_debug.led_pattern`, then runs.

### Example GDB session

```
(gdb) continue                         # run to main() breakpoint
Breakpoint 1, main () at main.c:142

(gdb) print g_debug                    # inspect debug_info_t struct
$1 = {step_count = 0, irq_count = 0, last_irq_id = 0, led_pattern = 85}

(gdb) inspect-gicd                     # verify GIC Distributor config
=== GIC Distributor (0xFFFED000) ===
GICD_CTLR       (0xFFFED000):  0x00000001   ← forwarding enabled
GICD_ISENABLER1 (0xFFFED104):  0x00000100   ← bit 8 = ID 72 enabled

(gdb) hbreak irq_c_handler             # break on next button press
(gdb) continue

# Press KEY[0] on the board — GDB halts in IRQ handler
Breakpoint 2, irq_c_handler () at main.c:87

(gdb) print g_debug.last_irq_id        # confirm interrupt source
$2 = 72                                ← GIC ID 72 = f2h_irq0[0] = button_pio

(gdb) inspect-button-pio               # inspect FPGA peripheral
=== Button PIO (LW H2F 0xFF201000) ===
EDGE_CAP  (0xFF20100C):  0x00000001    ← KEY[0] press captured

(gdb) finish                           # run to end of IRQ handler
(gdb) print g_debug.irq_count          # verify counter incremented
$3 = 1
```

### Hardware breakpoints vs software breakpoints

On bare-metal ARM, software breakpoints (`break`) overwrite memory with a
`BKPT` instruction.  This works fine for OCRAM.  Hardware breakpoints
(`hbreak`) use the Cortex-A9 debug unit (two address comparators) and do not
modify memory — essential when debugging code in ROM or flash.

## Firmware design

### `debug_info_t` struct

```c
typedef struct {
    volatile uint32_t step_count;    /* main-loop iteration counter          */
    volatile uint32_t irq_count;     /* incremented by irq_c_handler         */
    volatile uint32_t last_irq_id;   /* GIC IAR value from last interrupt     */
    volatile uint8_t  led_pattern;   /* current LED output value              */
} debug_info_t;

debug_info_t g_debug;               /* non-static: GDB can find by name      */
```

`last_irq_id` captures the GIC Interrupt Acknowledge Register (GICC_IAR)
value so you can confirm the correct interrupt source at the GDB prompt.

### `__attribute__((noinline))`

`irq_c_handler()` and `main()` are decorated with `__attribute__((noinline))`
to guarantee they appear as distinct frames in the ARM backtrace.

### Watchpoint demo (KEY[1])

```c
/* KEY[1] resets step_count — fires the watchpoint on g_debug.led_pattern */
if (edges & 0x2) {
    g_debug.step_count = 0;
    g_debug.led_pattern = INITIAL_PATTERN;
    pio_write32(LED_PIO_BASE, g_debug.led_pattern);
}
```

## GIC register reference

| Register       | Address        | Description                                        |
|----------------|---------------|----------------------------------------------------|
| GICD_CTLR      | `0xFFFED000`  | Global enable for interrupt forwarding             |
| GICD_ISENABLER1| `0xFFFED104`  | Enable bits for SPI[32]–SPI[63] (IDs 64–95)       |
| GICD_IPRIORITYR| `0xFFFED448`  | Priority for ID 72 (byte offset 72×4 = 0x120)     |
| GICD_ITARGETSR | `0xFFFED848`  | CPU target for ID 72                               |
| GICC_CTLR      | `0xFFFEC100`  | CPU Interface enable                               |
| GICC_PMR       | `0xFFFEC104`  | Priority mask (0xFF = all interrupts pass)         |
| GICC_IAR       | `0xFFFEC10C`  | Interrupt Acknowledge (read to accept, clears CPU) |
| GICC_EOIR      | `0xFFFEC110`  | End Of Interrupt (write IAR value to signal done)  |

## Concepts covered

- OpenOCD `aji_client` interface for ARM JTAG on Intel SoC
- Hardware breakpoints on Cortex-A9 (debug unit comparators)
- Loading bare-metal ELF over JTAG via OpenOCD `load_image`
- GIC register inspection at the GDB prompt
- `GICC_IAR` pattern for safe IRQ acknowledge in ISR
- Watchpoints on struct fields in OCRAM
- ARM exception vector table layout and mode-specific stacks
