# 07 — HPS Hardware Interrupt Demo

Extends project 05 with **ARM GIC (Generic Interrupt Controller) interrupt
handling**.  Push-button presses on KEY[0] and KEY[1] are captured by an
FPGA button PIO, forwarded through the FPGA-to-HPS interrupt fabric, and
dispatched to a bare-metal C interrupt handler running on the Cortex-A9.

## Architecture

```
                      ┌─────────────────────────────────────────────────┐
                      │       Cyclone V FPGA Fabric                      │
KEY[1:0] ──► button_pio ──► irq ──► f2h_irq0[0] ──► GIC SPI[40]        │
                      │                                                   │
                      │ LW H2F Bridge (0xFF200000)                        │
LED[7:0] ◄── led_pio ◄── AXI ◄───────────────────── Cortex-A9           │
                      └─────────────────────────────────────────────────┘
                                          │
                              ┌───────────▼──────────────┐
                              │  Cortex-A9 (SVC mode)     │
                              │  IRQ received → irq_entry │
                              │  → irq_c_handler()        │
                              │  → GICC_EOIR (end of int) │
                              └──────────────────────────┘
```

### Interrupt routing chain

```
button_pio.irq (level HIGH while edge-capture ≠ 0)
  → hps_0.f2h_irq0[0]          (FPGA-to-HPS interrupt line 0)
  → GIC SPI[40]                 (Shared Peripheral Interrupt 40)
  → GIC interrupt ID 72         (SPI[40] = ID 32 + 40 = 72)
  → Cortex-A9 IRQ exception     (CPSR enters IRQ mode)
  → irq_entry (startup.S)       (saves caller-saved regs, adjusts LR)
  → irq_c_handler() (main.c)    (reads IAR, clears PIO, writes EOIR)
```

### Memory map (ARM address space)

| Peripheral              | ARM address    | Size  |
|-------------------------|---------------|-------|
| HPS On-Chip RAM (OCRAM) | `0xFFFF0000`   | 64 KB |
| GIC CPU Interface       | `0xFFFEC100`   | 256 B |
| GIC Distributor         | `0xFFFED000`   | 4 KB  |
| H2F LW Bridge base      | `0xFF200000`   | 2 MB  |
| LED PIO DATA            | `0xFF200000`   | 4 B   |
| Button PIO DATA         | `0xFF201000`   | 4 B   |
| Button PIO IRQ_MASK     | `0xFF201008`   | 4 B   |
| Button PIO EDGE_CAPTURE | `0xFF20100C`   | 4 B   |

### OCRAM stack layout

The 64 KB OCRAM is used for code, data, and two ARM exception-mode stacks:

```
0xFFFFFFFF ─── _irq_stack_top  (IRQ mode stack, 512 B, grows ↓)
0xFFFFFDFF ─── _svc_stack_top  (SVC mode stack, rest of OCRAM, grows ↓)
               ↑
               .bss / .data / .text / .startup
0xFFFF0000
```

## Directory structure

```
07_hps_interrupts/
├── doc/
│   └── README.md           ← this file
├── hdl/
│   └── de10_nano_top.vhd   ← VHDL top-level; key(1:0) input, no reset port
├── qsys/
│   └── hps_system.tcl      ← Platform Designer script; button_pio + HPS
├── quartus/
│   ├── Makefile            ← full build orchestrator (two-pass + ARM app)
│   ├── de10_nano_project.tcl
│   ├── de10_nano_pin_assignments.tcl
│   └── de10_nano.sdc
├── scripts/
│   ├── de10_nano_hps.cfg   ← OpenOCD configuration
│   ├── load_hps_elf.gdb    ← GDB helper script
│   └── patch_oct.py        ← Quartus DDR3 OCT workaround
└── software/
    └── app/
        ├── startup.S       ← IRQ + SVC mode setup, irq_entry handler stub
        ├── linker.ld       ← dual stack symbols (_irq_stack_top, _svc_stack_top)
        ├── main.c          ← GIC init, button PIO init, irq_c_handler()
        └── Makefile
```

## Building

```bash
docker run --rm \
  -v /path/to/cvsoc:/work \
  cvsoc/quartus:23.1 \
  bash -c "cd /work/07_hps_interrupts/quartus && make all"
```

| Step | Make target  | Tool                        | Output                        |
|------|--------------|-----------------------------|-------------------------------|
| 1    | `qsys`       | `qsys-script` + `qsys-generate` | `qsys/hps_system/`        |
| 1b   |              | `patch_oct.py`              | patched DDR3 source           |
| 2    | `project`    | `quartus_sh -t`             | `.qpf`, `.qsf`                |
| 3    | `compile1`   | `quartus_sh --flow compile` | `.sof` (pass 1, no pin info)  |
| 4    | `compile2`   | `quartus_sh --flow compile` | `.sof` (pass 2, with IO pins) |
| 5    | `app`        | `arm-linux-gnueabihf-gcc`   | `hps_interrupts.elf/.bin`     |

## ARM exception mode and stack setup

The Cortex-A9 has separate banked registers (SP, LR, SPSR) for each exception
mode.  `startup.S` initialises both before calling `main()`:

```asm
msr cpsr_c, #0xD2     /* switch to IRQ mode (I=1, F=1 = both disabled) */
ldr sp, =_irq_stack_top

msr cpsr_c, #0xD3     /* switch to SVC mode (I=1, F=1) */
ldr sp, =_svc_stack_top
                      /* main() executes here in SVC mode */
```

IRQs remain disabled until `main()` calls `cpsie i` after the GIC is fully
configured.

## IRQ entry stub

When an IRQ fires, the CPU switches to IRQ mode and branches to the vector at
offset `0x18`.  `irq_entry` in `startup.S`:

1. Subtracts 4 from `LR_irq` (ARM architecture errata: LR points 4 bytes past
   the interrupted instruction).
2. Saves caller-saved registers (`r0–r3`, `r12`, `lr`) to the IRQ stack.
3. Calls `irq_c_handler()` (C function in `main.c`).
4. Restores registers.  The `^` suffix on `pop {…, pc}^` copies `SPSR_irq`
   into `CPSR`, atomically restoring the interrupted mode and re-enabling IRQs.

## GIC configuration

`gic_init()` in `main.c` configures the GIC for interrupt ID 72:

```c
GICD_CTLR = 0;                                          /* disable distributor */
GICD_IPRIORITYR(72) = 0xA0;                             /* set priority */
GICD_ITARGETSR(72)  = 0x01;                             /* route to CPU 0 */
GICD_ISENABLER(72 / 32) = (1u << (72 % 32));            /* unmask */
GICD_CTLR = 1;                                          /* re-enable distributor */
GICC_PMR  = 0xF0;                                       /* priority mask */
GICC_CTLR = 1;                                          /* enable CPU interface */
```

## ISR logic (`irq_c_handler`)

```c
void irq_c_handler(void)
{
    uint32_t iar    = GICC_IAR;          /* acknowledge; captures interrupt ID */
    uint32_t irq_id = iar & 0x3FF;

    if (irq_id == 72) {
        uint32_t edges = BUTTON_PIO_EDGE_CAP;
        BUTTON_PIO_EDGE_CAP = edges;     /* clear BEFORE processing */

        if (edges & 1) {                 /* KEY[0]: barrel-rotate LED pattern */
            g_led_pattern = (g_led_pattern << 1) | (g_led_pattern >> 7);
        }
        if (edges & 2) {                 /* KEY[1]: show press count */
            g_press_count++;
            g_led_pattern = (uint8_t)g_press_count;
        }
    }

    GICC_EOIR = iar;                     /* end-of-interrupt */
}
```

Reading `GICC_IAR` both acknowledges the interrupt (prevents re-entry) and
returns the interrupt ID.  Writing `GICC_EOIR` signals the GIC that the handler
is complete and the interrupt line can be deactivated.

## Programming the board

```bash
# Program FPGA (JTAG, not persistent)
quartus_pgm -m jtag -o "p;output_files/07_hps_interrupts.sof"

# Download ELF to OCRAM and run
make download-elf
```

## Concepts covered

- ARM GIC v1 (Distributor + CPU Interface) configuration from bare-metal C
- FPGA-to-HPS interrupt routing (F2H IRQ → GIC SPI → interrupt ID)
- ARM exception modes (SVC, IRQ) and banked register organisation
- IRQ exception entry stub in assembly (`irq_entry`, `pop {…, pc}^`)
- Dual exception-mode stacks in a flat OCRAM memory map
- `volatile` for shared state between interrupt and main context
- End-of-interrupt protocol with `GICC_IAR` / `GICC_EOIR`
