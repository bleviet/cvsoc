# Phase 4 Tutorial — Hardware Interrupts on Nios II and HPS

> **Series:** cvsoc — Stepping into advanced FPGA development on the DE10-Nano  
> **Phase:** 4 of 6  
> **Difficulty:** Intermediate-Advanced — you have completed phases 0–3 and are comfortable with Platform Designer, the Quartus compile flow, and ARM bare-metal programming

---

## What you will build

This phase covers two standalone projects that each add **interrupt-driven push-button control** to the LED demonstrations from Phase 2 and Phase 3.

**Project 06 — Nios II hardware interrupt (HAL-based)**

A Nios II soft-core system where pressing KEY[0] or KEY[1] generates an interrupt. A HAL-registered ISR responds to each press — rotating the LED pattern or showing a press counter — without any polling in the main loop.

```
           ┌──────────────────────────────────────────────────────────────┐
           │                 nios2_system (Platform Designer)              │
           │                                                                │
FPGA_CLK1_50 ──► clk_bridge ──► nios2 CPU                                │
                              ├──► on-chip RAM (32 KB)    0x00000000      │
                              ├──► JTAG UART              0x00010100  IRQ 0│
                              ├──► System ID              0x00010108       │
                              ├──► LED PIO (8-bit output) 0x00010010 ──► LED[7:0]
                              └──► button_pio (2-bit in)  0x00010020  IRQ 1│
                                       ▲                                   │
                              KEY[1:0] ┘                                   │
           └──────────────────────────────────────────────────────────────┘
```

**Project 07 — ARM HPS hardware interrupt (bare-metal GIC)**

The same button-press behaviour on the ARM Cortex-A9 HPS, using the ARM Generic Interrupt Controller. The push-button PIO in the FPGA fabric drives an FPGA-to-HPS interrupt line, which the GIC routes to the CPU as exception ID 72.

```
                  ┌──────────────────────────────────────────────────────────┐
                  │       Cyclone V FPGA Fabric                               │
KEY[1:0] ──► button_pio ──► irq ──► hps_0.f2h_irq0[0] ──► GIC SPI[40]     │
                  │                                                            │
                  │ LW H2F Bridge (0xFF200000)                                 │
LED[7:0] ◄── led_pio ◄── AXI ◄───────────────────────── Cortex-A9           │
                  └──────────────────────────────────────────────────────────┘
                                  │
                      ┌───────────▼──────────────────────┐
                      │  Cortex-A9 SVC mode               │
                      │  IRQ → irq_entry (startup.S)      │
                      │    → irq_c_handler() (main.c)     │
                      │    → GICC_EOIR                     │
                      └──────────────────────────────────┘
```

Every step — hardware generation, bitstream compilation, and firmware build — is driven from the command line inside a Docker container. No GUI required.

---

## Prerequisites

| Requirement | Details |
|---|---|
| **Docker** | `cvsoc/quartus:23.1` image available locally |
| **Repository** | `git clone` of `bleviet/cvsoc`; phases 0–3 already working |
| **Phase 2** | Nios II Platform Designer flow, HAL BSP generation understood |
| **Phase 3** | HPS bare-metal startup, LW H2F bridge, OCRAM execution understood |
| **Board** | Terasic DE10-Nano (Cyclone V `5CSEBA6U23I7`) |
| **JTAG cable** | USB-Blaster (built in on the DE10-Nano) |

Verify the Docker image is present before continuing:

```bash
docker images | grep cvsoc/quartus
# Expected: cvsoc/quartus   23.1   ...
```

---

## Concepts in 5 minutes

Both projects build on the same edge-capture PIO peripheral, but route the interrupt signal through two completely different interrupt controllers. Read these ideas before touching any file.

### Interrupt-driven vs polled I/O

Phase 2 and Phase 3 both polled hardware state in a `while(1)` loop. Polling works for slow, predictable events, but it wastes CPU cycles and can miss fast events between checks. **Interrupts** invert the relationship: the peripheral signals the CPU only when something happens. The CPU's response — the **Interrupt Service Routine (ISR)** — runs to completion, then control returns to wherever the main loop was.

### The altera_avalon_pio edge-capture register

The PIO peripheral used in both projects is configured in **falling-edge capture** mode. When a push-button (active-LOW) is pressed, the PIO captures the falling edge in a sticky register: the corresponding bit in the edge-capture register is latched HIGH and stays HIGH until firmware explicitly clears it.

This is the key to interrupt-driven button handling:

- The PIO's IRQ output is asserted (HIGH) while the edge-capture register has any bit set that also has a `1` in the IRQ mask register.
- Reading the edge-capture register tells the ISR exactly which button(s) were pressed.
- **Writing `1` to a bit of the edge-capture register clears it.**

The ISR must clear the edge-capture register **before** acting on the captured edge. If it clears at the end, any edge arriving *during* the ISR is silently overwritten and the interrupt pulse is lost.

### Nios II interrupt controller (IIC)

The Nios II processor has a simple **Internal Interrupt Controller (IIC)**. It supports up to 32 interrupt lines, each assigned a unique `irqNumber` (0 = highest priority). In Platform Designer, you wire each peripheral's `.irq` interface to the CPU's `.irq` interface and set an `irqNumber`.

The HAL provides `alt_ic_isr_register()` to register a C function as an ISR. The HAL startup code enables global interrupts automatically after the first registration call. You do not need to manually configure any interrupt controller registers.

### ARM Generic Interrupt Controller (GIC)

The Cyclone V ARM Cortex-A9 uses the industry-standard **ARM GIC v1**, which consists of two functional blocks:

- **GIC Distributor (GICD)** at `0xFFFED000`: manages interrupt sources — enables, priorities, and CPU routing for each of the up to 256 interrupts.
- **GIC CPU Interface (GICC)** at `0xFFFEC100`: per-CPU; forwards the highest-priority enabled interrupt to the CPU. The CPU reads the **Interrupt Acknowledge Register (IAR)** to identify the interrupt and writes the **End-of-Interrupt Register (EOIR)** when the handler is done.

Interrupts from FPGA peripherals arrive as **FPGA-to-HPS (F2H)** interrupt lines. F2H line 0 maps to **SPI[40]**, which corresponds to **GIC interrupt ID 72** (because SPI interrupts are numbered from ID 32: SPI[40] = 32 + 40 = 72). Unlike Nios II, the ARM GIC requires explicit C code to configure every aspect of delivery: priority, target CPU, enable bit, priority threshold, and the CPU interface enable.

### ARM exception modes and IRQ return

The Cortex-A9 has seven operating modes, each with its own banked copies of SP and LR (and SPSR for exception modes). When an IRQ fires, the CPU automatically:

1. Copies the current CPSR into **SPSR_irq**.
2. Switches to **IRQ mode**.
3. Saves the return address in **LR_irq** (pointing 4 bytes past the interrupted instruction due to the ARM pipeline).
4. Jumps to the IRQ vector at exception table offset `0x18`.

The IRQ entry stub must:

1. Subtract 4 from LR_irq to get the correct return address.
2. Save caller-saved registers to the IRQ mode stack.
3. Call the C handler.
4. Restore registers and execute `ldmfd sp!, {..., pc}^` — the `^` suffix copies SPSR_irq back into CPSR, atomically restoring the interrupted mode and re-enabling IRQs.

> **GNU assembler note:** The `^` SPSR-restore suffix is only valid after an `ldm` or `stm` mnemonic, **not** after `pop`. Always write `ldmfd sp!, {r0-r3, r12, pc}^`, not `pop {r0-r3, r12, pc}^`.

---

## Part A — Project 06: Nios II hardware interrupts

### Project layout

```
06_nios2_interrupts/
├── doc/
│   └── README.md
├── hdl/
│   └── de10_nano_top.vhd       ← adds key(1:0) port; button_pio connection
├── qsys/
│   └── nios2_system.tcl        ← adds button_pio, IRQ wiring
├── quartus/
│   ├── Makefile
│   ├── de10_nano_project.tcl
│   ├── de10_nano_pin_assignments.tcl
│   └── de10_nano.sdc
└── software/
    ├── bsp/                    ← generated (not committed)
    └── app/
        ├── Makefile
        └── main.c
```

---

### Step A1 — Extend the Platform Designer system

`qsys/nios2_system.tcl` is the complete description of every component and connection. It extends the Phase 2 system by adding a `button_pio` instance and wiring its IRQ.

#### A1.1 Add and configure the button PIO

The new component is a 2-bit input PIO with falling-edge capture and IRQ generation enabled:

```tcl
add_instance button_pio altera_avalon_pio
set_instance_parameter_value button_pio width        {2}
set_instance_parameter_value button_pio direction    {Input}
set_instance_parameter_value button_pio resetValue   {0}
set_instance_parameter_value button_pio captureEdge  {1}
set_instance_parameter_value button_pio edgeType     {FALLING}
set_instance_parameter_value button_pio generateIRQ  {1}
set_instance_parameter_value button_pio irqType      {EDGE}
```

The `irqType {EDGE}` setting means the PIO asserts its IRQ output for one clock cycle per captured edge (the edge-capture register provides the sticky latch). `irqType {LEVEL}` would hold the IRQ high for the duration of the edge-capture; either works here because the edge-capture register is cleared inside the ISR.

#### A1.2 Connect clocks and resets

Extend the existing clock and reset wiring:

```tcl
add_connection clk_0.out_clk   button_pio.clk
add_connection reset_bridge.out_reset button_pio.reset
```

#### A1.3 Connect the Avalon data bus

The CPU must be able to read and write the button PIO's registers (data, IRQ mask, edge-capture):

```tcl
add_connection nios2.data_master button_pio.s1
set_connection_parameter_value nios2.data_master/button_pio.s1 baseAddress {0x00010020}
```

The base address `0x00010020` places the 16-byte PIO register window immediately after the LED PIO at `0x00010010`.

#### A1.4 Wire the IRQ

```tcl
add_connection nios2.irq button_pio.irq
set_connection_parameter_value nios2.irq/button_pio.irq irqNumber {1}
```

> **Key learning #1 — `nios2.irq` is the interrupt *receiver*, not the sender.**  
> `nios2.irq` is the CPU's IIC master. `button_pio.irq` is the sender (a slave interface). The connection is written `nios2.irq button_pio.irq` — the master first, then the slave. Lower `irqNumber` values have higher priority; the JTAG UART takes priority 0, so the button gets priority 1.

#### A1.5 Export the button pins

The PIO's physical pin connection must be exported to the Platform Designer top-level:

```tcl
add_interface button_external_connection conduit end
set_interface_property button_external_connection EXPORT_OF button_pio.external_connection
```

This produces a port `button_external_connection_export[1:0]` in the generated `nios2_system` entity.

---

### Step A2 — Update the VHDL top-level

`hdl/de10_nano_top.vhd` gains a `key` input port and connects it to the new exported conduit from Platform Designer.

Add the port to the entity declaration:

```vhdl
entity de10_nano_top is
  port (
    fpga_clk1_50   : in  std_logic;
    led            : out std_logic_vector(7 downto 0);
    key            : in  std_logic_vector(1 downto 0)  -- KEY[1:0], active-LOW
  );
end entity de10_nano_top;
```

Update the component declaration to include the new port:

```vhdl
component nios2_system is
  port (
    clk_clk                               : in  std_logic;
    reset_reset                           : in  std_logic;
    led_external_connection_export        : out std_logic_vector(7 downto 0);
    button_external_connection_export     : in  std_logic_vector(1 downto 0)
  );
end component nios2_system;
```

Wire it in the instantiation:

```vhdl
nios2_system_inst : nios2_system
  port map (
    clk_clk                           => fpga_clk1_50,
    reset_reset                       => power_on_reset,
    led_external_connection_export    => led,
    button_external_connection_export => key
  );
```

The push-buttons are active-LOW on the DE10-Nano. The PIO is configured to capture **falling edges**, so pressing a button (pulling the signal LOW) is the trigger — no inversion is needed in VHDL.

---

### Step A3 — Build the hardware

```bash
docker run --rm \
  -v /path/to/cvsoc:/work \
  cvsoc/quartus:23.1 \
  bash -c "cd /work/06_nios2_interrupts/quartus && make all"
```

`make all` runs the full sequence: `qsys` → `project` → `compile` → `bsp` → `app`.

| Step | Target | Tool | Output |
|---|---|---|---|
| 1 | `qsys` | `qsys-script` + `qsys-generate` | `qsys/nios2_system_gen/` |
| 2 | `project` | `quartus_sh -t` | `.qpf`, `.qsf` |
| 3 | `compile` | `quartus_sh --flow compile` | `de10_nano.sof` |
| 4 | `bsp` | `nios2-bsp-create-settings` | `software/bsp/` |
| 5 | `app` | `nios2-elf-gcc` | `software/app/nios2_interrupts.elf` |

Expected final output:

```
nios2-elf-size nios2_interrupts.elf
   text    data     bss     dec     hex filename
   2176       4      12    2192     890 nios2_interrupts.elf
```

---

### Step A4 — Write the C firmware

The firmware is in `software/app/main.c`. There is no loop that polls the buttons; all button logic lives in the ISR.

#### A4.1 Declare shared state

```c
static volatile uint8_t  g_led_pattern  = 0x01;
static volatile uint32_t g_press_count  = 0;
```

Both variables are `volatile` because they are written by the ISR and read by the main loop. Without `volatile`, the compiler may cache their values in registers and the main loop would never observe updates made in interrupt context.

#### A4.2 Write the ISR

```c
static void button_isr(void *context)
{
    (void)context;

    uint32_t edges = IORD_ALTERA_AVALON_PIO_EDGE_CAP(BUTTON_PIO_BASE);
    IOWR_ALTERA_AVALON_PIO_EDGE_CAP(BUTTON_PIO_BASE, edges); /* clear BEFORE acting */

    if (edges & 0x1u) {
        g_led_pattern = (uint8_t)((g_led_pattern << 1) | (g_led_pattern >> 7));
    }
    if (edges & 0x2u) {
        g_press_count++;
        g_led_pattern = (uint8_t)g_press_count;
    }
}
```

The edge-capture register is cleared **before** the LED pattern is updated. The edge-capture register is sticky: if another button press arrives while this ISR is running, clearing it afterward would erase that new edge. Clearing first preserves any edge that arrives mid-ISR.

#### A4.3 Register the ISR and configure the PIO

```c
int main(void)
{
    IOWR_ALTERA_AVALON_PIO_EDGE_CAP(BUTTON_PIO_BASE, 0x3u); /* clear stale edges */
    IOWR_ALTERA_AVALON_PIO_IRQ_MASK(BUTTON_PIO_BASE, 0x3u); /* enable both buttons */

    alt_ic_isr_register(
        BUTTON_PIO_IRQ_INTERRUPT_CONTROLLER_ID,
        BUTTON_PIO_IRQ,
        button_isr,
        NULL, NULL
    );

    while (1) {
        IOWR_ALTERA_AVALON_PIO_DATA(LED_PIO_BASE, g_led_pattern);
    }
}
```

`BUTTON_PIO_IRQ_INTERRUPT_CONTROLLER_ID`, `BUTTON_PIO_IRQ`, and `BUTTON_PIO_BASE` are all `#define`d in the BSP-generated `system.h`. The HAL enables global interrupts automatically after the first `alt_ic_isr_register()` call.

> **ISR rules:** Never call HAL functions that may block (e.g. `printf`, `usleep`) inside an ISR. Keep the ISR short: read hardware state, clear hardware flags, update shared variables, return.

---

### Step A5 — Program the board and verify

**Program the FPGA:**

```bash
# On the host (adjust the .sof path as needed)
quartus_pgm -m jtag -o "p;06_nios2_interrupts/quartus/output_files/de10_nano.sof@2"
```

**Download the ELF and start the processor:**

```bash
docker run --rm --privileged \
  -v /path/to/cvsoc:/work \
  cvsoc/quartus:23.1 \
  nios2-download -g /work/06_nios2_interrupts/software/app/nios2_interrupts.elf
```

**Expected behaviour:**

- `LED[0]` is lit after reset (initial pattern `0x01`).
- Pressing **KEY[0]** barrel-rotates the LED pattern one position to the left on each press.
- Pressing **KEY[1]** increments a counter; the lower 8 bits of the count are shown on the LEDs.
- The main loop runs continuously; there is no polling delay, so the LED display updates instantly.

---

## Part B — Project 07: ARM HPS hardware interrupts

### Project layout

```
07_hps_interrupts/
├── doc/
│   └── README.md
├── hdl/
│   └── de10_nano_top.vhd       ← key(1:0) input; button_pio + F2H IRQ export
├── qsys/
│   └── hps_system.tcl          ← adds button_pio + F2H IRQ connection
├── quartus/
│   ├── Makefile
│   ├── de10_nano_project.tcl
│   ├── de10_nano_pin_assignments.tcl
│   └── de10_nano.sdc
├── scripts/
│   ├── de10_nano_hps.cfg       ← OpenOCD config
│   ├── load_hps_elf.gdb        ← GDB helper
│   └── patch_oct.py            ← Quartus DDR3 OCT workaround
└── software/
    └── app/
        ├── startup.S
        ├── linker.ld
        ├── main.c
        └── Makefile
```

---

### Step B1 — Extend the Platform Designer system

`qsys/hps_system.tcl` builds on the Phase 3 system by adding a `button_pio` and routing its IRQ through the FPGA-to-HPS interrupt fabric.

#### B1.1 Enable the F2H interrupt interface

By default the `altera_hps` component does not expose any FPGA-to-HPS interrupt lines. One parameter unlocks both `f2h_irq0` and `f2h_irq1`:

```tcl
set_instance_parameter_value hps_0 F2SINTERRUPT_Enable {1}
```

Without this, the `f2h_irq0` interface does not exist and any attempt to connect to it will fail at `qsys-generate` time.

> **Key learning #2 — `F2SINTERRUPT_Enable` must be set explicitly.**  
> This parameter is absent from the default `altera_hps` configuration. Forgetting it produces:  
> `Error: hps_0: No interface named f2h_irq0`  
> Add `set_instance_parameter_value hps_0 F2SINTERRUPT_Enable {1}` before any connection that references `hps_0.f2h_irq0`.

#### B1.2 Add and configure the button PIO

Identical to project 06 — same parameters, same edge-capture configuration:

```tcl
add_instance button_pio altera_avalon_pio
set_instance_parameter_value button_pio width        {2}
set_instance_parameter_value button_pio direction    {Input}
set_instance_parameter_value button_pio resetValue   {0}
set_instance_parameter_value button_pio captureEdge  {1}
set_instance_parameter_value button_pio edgeType     {FALLING}
set_instance_parameter_value button_pio generateIRQ  {1}
set_instance_parameter_value button_pio irqType      {EDGE}
```

#### B1.3 Connect clocks, resets, and the data bus

```tcl
add_connection clk_0.clk         button_pio.clk
add_connection hps_0.h2f_reset   button_pio.reset

add_connection hps_0.h2f_lw_axi_master button_pio.s1
set_connection_parameter_value \
  hps_0.h2f_lw_axi_master/button_pio.s1 baseAddress {0x00001000}
```

The base address `0x00001000` within the 2 MB LW bridge window gives the button PIO the ARM address `0xFF200000 + 0x1000 = 0xFF201000`.

#### B1.4 Wire the FPGA-to-HPS IRQ

```tcl
add_connection hps_0.f2h_irq0 button_pio.irq
set_connection_parameter_value hps_0.f2h_irq0/button_pio.irq irqNumber {0}
```

> **Key learning #3 — `hps_0.f2h_irq0` is the bus master, not the sender.**  
> This connection is the opposite of the Nios II case. `hps_0.f2h_irq0` is a 32-bit wide IRQ collector bus that is a **START** interface (it receives IRQ signals from FPGA peripherals and forwards them to the GIC). `button_pio.irq` is the **END** interface (the signal source). The connection is therefore written `hps_0.f2h_irq0 button_pio.irq` — the collector first, then the sender. Writing it the other way (`button_pio.irq hps_0.f2h_irq0`) will produce a direction error at `qsys-generate` time.
>
> With `irqNumber {0}`, `button_pio.irq` drives bit 0 of the F2H IRQ bus. This maps to **GIC SPI[40]** → **GIC interrupt ID 72**.

---

### Step B2 — Update the VHDL top-level

`hdl/de10_nano_top.vhd` gains a `key` input port and connects it to the exported `button_external_connection_export`:

```vhdl
entity de10_nano_top is
  port (
    fpga_clk1_50   : in  std_logic;
    led            : out std_logic_vector(7 downto 0);
    key            : in  std_logic_vector(1 downto 0);
    hps_ddr3_addr  : out std_logic_vector(14 downto 0);
    -- ... other DDR3 ports unchanged
  );
end entity de10_nano_top;
```

The `hps_system` component gains the new conduit:

```vhdl
component hps_system is
  port (
    clk_clk                           : in  std_logic;
    memory_mem_a                      : out std_logic_vector(14 downto 0);
    -- ... DDR3 ports ...
    led_external_connection_export    : out std_logic_vector(7 downto 0);
    button_external_connection_export : in  std_logic_vector(1 downto 0)
  );
end component hps_system;
```

Instantiation:

```vhdl
hps_system_inst : hps_system
  port map (
    clk_clk                           => fpga_clk1_50,
    -- ... DDR3 ports ...
    led_external_connection_export    => led,
    button_external_connection_export => key
  );
```

---

### Step B3 — Build the hardware

```bash
docker run --rm \
  -v /path/to/cvsoc:/work \
  cvsoc/quartus:23.1 \
  bash -c "cd /work/07_hps_interrupts/quartus && make all"
```

The HPS project requires a **two-pass compile** because the HPS pin assignments are derived from the first pass's fitter output:

| Step | Target | Tool | Output |
|---|---|---|---|
| 1 | `qsys` | `qsys-script` + `qsys-generate` + `patch_oct.py` | `qsys/hps_system/synthesis/` |
| 2 | `project` | `quartus_sh -t` | `.qpf`, `.qsf` |
| 3 | `compile1` | `quartus_sh --flow compile` | `.sof` (pass 1, no HPS pin data) |
| 4 | `compile2` | `quartus_sh --flow compile` | `de10_nano.sof` (pass 2, with HPS pin data) |
| 5 | `app` | `arm-linux-gnueabihf-gcc` | `hps_interrupts.elf`, `hps_interrupts.bin` |

Expected application size:

```
arm-linux-gnueabihf-size hps_interrupts.elf
   text    data     bss     dec     hex filename
    598       4       8     610     262 hps_interrupts.elf
```

---

### Step B4 — Write the ARM firmware

The firmware consists of three files: `startup.S` (exception vector table and IRQ entry stub), `linker.ld` (OCRAM memory layout with dual stacks), and `main.c` (GIC configuration and ISR).

#### B4.1 Linker script — dual exception-mode stacks

`software/app/linker.ld` places all sections in OCRAM and defines two stack pointers as address constants:

```ld
MEMORY
{
    OCRAM (rwx) : ORIGIN = 0xFFFF0000, LENGTH = 64K
}

SECTIONS
{
    .startup : { *(.startup) } > OCRAM
    .text    : { *(.text*) *(.rodata*) } > OCRAM
    .data    : { *(.data*) } > OCRAM
    .bss     : {
        __bss_start = .;
        *(.bss*) *(COMMON)
        __bss_end = .;
    } > OCRAM

    _irq_stack_top = ORIGIN(OCRAM) + LENGTH(OCRAM) - 4;   /* 0xFFFFFFFC */
    _svc_stack_top = _irq_stack_top - 0x200;               /* 0xFFFFFDFC */
}
```

`_irq_stack_top` and `_svc_stack_top` are not sections — they are plain linker symbols (address constants) that `startup.S` loads into SP for each exception mode.

#### B4.2 Startup code — stacks, BSS, and exception table

`software/app/startup.S` performs four jobs in order:

**1. Exception vector table** — placed at the very start of the `.startup` section, which the linker puts at `0xFFFF0000`:

```asm
_vectors:
    b   _start      /* 0x00: Reset */
    b   _start      /* 0x04: Undefined Instruction */
    b   _start      /* 0x08: SVC */
    b   _start      /* 0x0C: Prefetch Abort */
    b   _start      /* 0x10: Data Abort */
    b   _start      /* 0x14: Reserved */
    b   irq_entry   /* 0x18: IRQ ← this is the interrupt vector */
    b   _start      /* 0x1C: FIQ */
```

> **Key learning #4 — The exception table must start at offset 0 of the binary.**  
> OpenOCD loads the binary at `0xFFFF0000`. When an IRQ fires, the CPU jumps to its vector table base address (`0xFFFF0000` after OCRAM remap) plus offset `0x18`. If the exception table is not at offset 0, the CPU will branch to whatever code happens to be at that address — likely a garbage instruction.

**2. IRQ mode stack setup:**

```asm
_start:
    msr  cpsr_c, #0xD2   /* switch to IRQ mode (I=1, F=1 = both disabled) */
    ldr  sp, =_irq_stack_top
```

**3. SVC mode stack setup** (all C code runs in SVC mode):

```asm
    msr  cpsr_c, #0xD3   /* switch to SVC mode (I=1, F=1) */
    ldr  sp, =_svc_stack_top
```

Both stacks are set up while IRQ and FIQ are disabled (`I=1, F=1` in CPSR). IRQs remain disabled here; `main()` enables them after GIC configuration is complete.

**4. IRQ entry stub:**

```asm
irq_entry:
    sub   lr, lr, #4               /* Adjust LR: points at interrupted instruction */
    stmfd sp!, {r0-r3, r12, lr}    /* Save scratch regs + adjusted return addr */
    bl    irq_c_handler             /* Call C dispatcher */
    ldmfd sp!, {r0-r3, r12, pc}^   /* Restore; ^ copies SPSR_irq → CPSR */
```

The `sub lr, lr, #4` adjustment is necessary because ARM fills the pipeline three instructions ahead. When the IRQ fires, `LR_irq` already points four bytes past the instruction that was interrupted. Subtracting 4 recovers the correct return address. The `^` on `ldmfd` atomically restores CPSR from SPSR_irq, returning to SVC mode with IRQs re-enabled.

#### B4.3 GIC initialisation

`gic_init()` in `main.c` configures the GIC for interrupt ID 72:

```c
static void gic_init(void)
{
    GICD_CTLR = 0u;                                           /* 1. disable distributor */
    GICD_IPRIORITYR(BUTTON_GIC_IRQ) = 0xA0u;                 /* 2. set priority */
    GICD_ITARGETSR(BUTTON_GIC_IRQ)  = 0x01u;                 /* 3. route to CPU 0 */
    GICD_ISENABLER(BUTTON_GIC_IRQ / 32u) =                    /* 4. unmask */
        (1u << (BUTTON_GIC_IRQ % 32u));
    GICD_CTLR = 1u;                                           /* 5. re-enable */
    __asm__ volatile("dsb" ::: "memory");                     /*    memory barrier */
    GICC_PMR  = 0xF0u;                                        /* 6. priority mask */
    GICC_CTLR = 1u;                                           /* 7. enable CPU IF */
}
```

Each step matters:

| Step | Register | Value | Reason |
|---|---|---|---|
| 1 | `GICD_CTLR` | `0` | Disable distributor before changing config |
| 2 | `GICD_IPRIORITYR[72]` | `0xA0` | Set priority (lower = higher priority; 0xA0 is mid-range) |
| 3 | `GICD_ITARGETSR[72]` | `0x01` | Route to CPU 0 (bit 0) |
| 4 | `GICD_ISENABLER[2]` bit 8 | `1` | Unmask interrupt 72 (72/32=2, 72%32=8) |
| 5 | `GICD_CTLR` | `1` | Re-enable distributor |
| 6 | `GICC_PMR` | `0xF0` | Forward any interrupt with priority ≤ 0xF0 |
| 7 | `GICC_CTLR` | `1` | Enable this CPU's interface |

> **Key learning #5 — `GICD_IPRIORITYR` and `GICD_ITARGETSR` are byte-addressed.**  
> Unlike most GIC registers (which are 32-bit word registers indexed in groups of 32 interrupts), `IPRIORITYR` and `ITARGETSR` use **one byte per interrupt**. The correct C access is `*(volatile uint8_t *)(GICD_BASE + 0x400 + irq_id)`, not a 32-bit read-modify-write.

#### B4.4 The C ISR dispatcher

```c
void irq_c_handler(void)
{
    uint32_t iar    = GICC_IAR;
    uint32_t irq_id = iar & 0x3FFu;      /* bits [9:0] = interrupt ID */

    if (irq_id == BUTTON_GIC_IRQ) {
        uint32_t edges = BUTTON_PIO_EDGE_CAP;
        BUTTON_PIO_EDGE_CAP = edges;      /* clear BEFORE processing */

        if (edges & 0x1u) {
            g_led_pattern = (uint8_t)((g_led_pattern << 1u) | (g_led_pattern >> 7u));
        }
        if (edges & 0x2u) {
            g_press_count++;
            g_led_pattern = (uint8_t)g_press_count;
        }
    }

    GICC_EOIR = iar;                      /* end-of-interrupt */
}
```

Reading `GICC_IAR` does two things at once: it **acknowledges** the interrupt (prevents the GIC from re-presenting the same interrupt until EOIR is written) and returns the interrupt ID. The function checks `irq_id == 72` to guard against spurious interrupts. After handling, writing the same `iar` value to `GICC_EOIR` signals the GIC that the handler is complete and the interrupt line can be deactivated.

#### B4.5 Main loop with watchdog management

The Cyclone V HPS has three watchdog timers that fire resets if not periodically serviced. `main()` initialises them with maximum timeout and kicks them from the main loop:

```c
void main(void)
{
    wdt_init();          /* maximise timeout; disable MPCore WDT */
    hps_bridge_init();   /* enable L3 remap + release bridge resets */
    gic_init();          /* configure GIC for interrupt ID 72 */
    button_pio_init();   /* clear stale edges; enable IRQ mask */

    __asm__ volatile("cpsie i" ::: "memory");  /* enable IRQ delivery */

    while (1) {
        wdt_kick();
        LED_PIO_DATA = g_led_pattern;
        delay(500000UL);
    }
}
```

`cpsie i` clears bit 7 (the I flag) of CPSR, enabling IRQ delivery to the CPU. This is called **after** `gic_init()` to ensure the GIC is fully configured before the first interrupt can arrive.

---

### Step B5 — Program the board and verify

**Program the FPGA:**

```bash
quartus_pgm -m jtag -o "p;07_hps_interrupts/quartus/output_files/de10_nano.sof@2"
```

The `@2` suffix selects device 2 in the JTAG chain (device 1 is the HPS ARM DAP; device 2 is the Cyclone V FPGA).

**Load the ARM binary and run:**

```bash
docker run --rm --privileged \
  -v /path/to/cvsoc:/work \
  cvsoc/quartus:23.1 \
  openocd \
    -f /work/07_hps_interrupts/scripts/de10_nano_hps.cfg \
    -c "init; halt" \
    -c "load_image /work/07_hps_interrupts/software/app/hps_interrupts.bin 0xFFFF0000 bin" \
    -c "resume 0xFFFF0000" \
    -c "exit"
```

`load_image` with the `bin` format loads the flat binary directly into OCRAM at `0xFFFF0000`. `resume 0xFFFF0000` releases the CPU and sets the PC to the OCRAM base where `_vectors` and then `_start` reside.

**Expected behaviour:**

- `LED[0]` is lit after the ARM binary starts (initial pattern `0x01`).
- Pressing **KEY[0]** barrel-rotates the LED pattern left.
- Pressing **KEY[1]** increments a counter and displays the lower 8 bits.
- The watchdog is continuously kicked from the main loop; the board does not reset on its own.

---

## Side-by-side comparison

| Aspect | 06 — Nios II (HAL) | 07 — HPS ARM (bare-metal GIC) |
|---|---|---|
| **Interrupt controller** | Nios II IIC (internal, simple) | ARM GIC v1 (Distributor + CPU Interface) |
| **IRQ routing** | `button_pio.irq → nios2.irq[1]` | `button_pio.irq → f2h_irq0[0] → SPI[40] → ID 72` |
| **ISR registration** | `alt_ic_isr_register()` (HAL) | Exception vector table (assembly) |
| **Global IRQ enable** | Automatic after first `alt_ic_isr_register()` | Manual `cpsie i` after `gic_init()` |
| **End-of-interrupt** | Implicit (HAL manages) | Explicit `GICC_EOIR = iar` |
| **IRQ return** | HAL managed | `ldmfd sp!, {r0-r3, r12, pc}^` |
| **Platform Designer key** | `irqNumber` on `nios2.irq` connection | `F2SINTERRUPT_Enable=1` + `f2h_irq0` connection direction |
| **Stack setup** | BSP linker script (single stack) | Hand-written linker script, two mode-specific stacks |

---

## Troubleshooting

### `Error: hps_0: No interface named f2h_irq0`

The `F2SINTERRUPT_Enable` parameter was not set. Add `set_instance_parameter_value hps_0 F2SINTERRUPT_Enable {1}` **before** any connection referencing `hps_0.f2h_irq0`.

### `qsys-generate` direction error on `button_pio.irq` connection

The connection direction was reversed. `hps_0.f2h_irq0` is the collector (START); `button_pio.irq` is the sender (END). The correct TCL line is:

```tcl
add_connection hps_0.f2h_irq0 button_pio.irq
```

not `add_connection button_pio.irq hps_0.f2h_irq0`.

### `Error: immediate value out of range` or assembler error on `pop^`

GNU assembler rejects the `^` SPSR-restore suffix after `push`/`pop`. Use the canonical LDM/STM form:

```asm
ldmfd sp!, {r0-r3, r12, pc}^   /* correct */
pop   {r0-r3, r12, pc}^        /* rejected by GNU as */
```

### `apt-get` fails inside Docker on the HPS project

The `cvsoc/quartus:23.1` container is based on Debian Stretch (EOL). The default `sources.list` points to dead mirrors. The Makefile `setup` target overwrites the sources list with the Debian snapshot archive:

```makefile
setup:
    echo "deb http://snapshot.debian.org/archive/debian/20220622T000000Z stretch main" \
      > /etc/apt/sources.list
    apt-get -o Acquire::Check-Valid-Until=false update -qq
    apt-get install -y -qq gcc-arm-linux-gnueabihf openocd
```

### LED does not respond to button presses (project 07)

If the main loop runs but interrupts never fire, check in order:

1. **FPGA programmed?** The `h2f_rst_n` signal stays asserted (LOW) if the FPGA is not configured. The button PIO is held in reset and cannot assert its IRQ.
2. **`F2SINTERRUPT_Enable` set?** Without it, the FPGA-to-HPS interrupt fabric is not instantiated.
3. **GIC ID correct?** F2H IRQ line 0 maps to SPI[40] = GIC ID 72. Verify `BUTTON_GIC_IRQ` is `72` in `main.c`.
4. **`cpsie i` called?** IRQs are disabled at startup; `main()` must call `cpsie i` after `gic_init()`.
5. **Edge-capture cleared at startup?** Stale edges before reset can prevent the PIO from asserting a fresh IRQ. `button_pio_init()` writes `0x3` to the edge-capture register to flush them.
