# 04c Nios V Dual Physical UART — Implementation Plan

> **For agentic workers:** Implement this plan task-by-task and use the checkbox
> (`- [ ]`) syntax to track progress. Do not treat generated Platform Designer,
> BSP, CMake, or Quartus outputs as source files.

**Goal:** Add a `04c_niosv_dual_uart` sibling project in which two FPGA-fabric
UART instances communicate through crossed jumper wires on the DE10-Nano GPIO
headers. UART B acts as a command client, UART A runs the existing LED command
shell, and the JTAG UART remains available as a supervisor and diagnostic
console.

**Architecture:** Start from `04b_niosv_led`. Add two fixed-rate Avalon UART
cores to the Nios V Platform Designer system and export their RX/TX signals to
four 3.3 V GPIO pins. Physically cross UART A TX to UART B RX and UART B TX to
UART A RX. Firmware runs the existing `CommandShell` on UART A, an asynchronous
request/response client on UART B, and a second small supervisor shell on JTAG
UART. Interrupt-backed software rings service both physical UARTs so neither
receiver overruns while the other endpoint is transmitting a multi-character
shell response.

**Tech Stack:** Quartus Prime Lite 25.1, Platform Designer Tcl, Nios V/m,
Avalon-MM UART IP, VHDL 2008, C++ freestanding firmware, Nios V HAL interrupts,
GNU Make, DE10-Nano GPIO headers, 3.3 V jumper wires.

## Intended data flow

```text
Host JTAG terminal
        |
        v
JTAG supervisor shell
        |
        | schedules link.get / link.set requests
        v
UART B client TX  ---- GPIO jumper ---->  UART A server RX
UART B client RX  <--- GPIO jumper -----  UART A server TX
                                                |
                                                v
                                      existing LED commands
                                                |
                                                v
                                            LED[7:0]
```

UART A and UART B are full-duplex transports; their server/client roles are a
firmware convention. UART B requests LED changes or status, UART A owns the LED
command handlers, and UART A returns the result to UART B.

## Global constraints

- Preserve `04b_niosv_led` as the simpler JTAG-UART-only lesson. Build `04c` as
  a sibling rather than changing `04b` into the dual-UART design.
- Target Quartus 25.1 only, matching the other Nios V projects.
- Keep JTAG UART in the design for supervision, logs, recovery, and comparison
  with physical UART.
- Use two independent FPGA-fabric UART IP instances at `115200 8N1`, with flow
  control disabled and a fixed baud rate derived from the 50 MHz system clock.
- Connect the two physical UARTs through external jumper wires. Do not connect
  them internally in VHDL; the GPIO path is part of the lesson.
- Use only confirmed 3.3 V FPGA GPIO pins. Never connect either UART to a 5 V
  serial output.
- Do not use the DE10-Nano's onboard HPS USB-UART as if it were directly wired
  to FPGA fabric.
- Cross outputs to inputs only: `A_TX -> B_RX` and `B_TX -> A_RX`. Never connect
  two TX outputs together.
- Keep the physical UART data path operational without `picocom`; both endpoints
  are inside the FPGA. A logic analyzer or RX-only USB-UART adapter is optional
  for observing a line.
- Use interrupt-backed RX/TX rings for both physical UARTs. A purely polling
  implementation is not acceptable: while UART A synchronously emits a shell
  response, UART B must continue draining received characters, and vice versa.
- Keep interrupt handlers bounded: move bytes and update counters only. Parsing,
  command execution, LED access, formatting, and JTAG output stay in the main
  loop.
- Keep the request client non-blocking. A JTAG command schedules work and returns;
  the main loop must continue polling the UART A server to complete that work.
- Do not run interactive echoing shells on both physical UARTs. Echoes and prompts
  can otherwise create feedback or unbounded chatter.
- Use BSP-generated `system.h` base addresses and IRQ identifiers. Do not place
  literal peripheral addresses in application logic or reusable transports.
- Preserve the fixed-buffer/no-allocation style of
  `common/software/niosv_shell/`.
- Verify exact UART IP parameter and interface names against the installed
  Quartus 25.1 component before committing the Qsys Tcl. Names listed below are
  expected values, not a substitute for `qsys-script` validation.

## Proposed hardware map

Keep the existing `04b` entries and add the physical UARTs in unused address
space:

| Peripheral | Base address | Proposed span | IRQ |
|---|---:|---:|---:|
| LED PIO | `0x00010010` | 16 B | — |
| JTAG UART | `0x00010100` | 8 B | 0 |
| System ID | `0x00010108` | 8 B | — |
| Nios V timer/SW agent | `0x00010140` | 64 B | internal |
| UART A, LED server | `0x00010200` | verify after generation | 1 |
| UART B, command client | `0x00010220` | verify after generation | 2 |
| Nios V debug module | `0x00100000` | 64 KB | internal |

If Platform Designer reports a larger UART span, move UART B upward rather than
allowing overlap. Record the final generated spans in the project README.

## Link behavior and supervisor commands

UART A receives the same textual commands already taught by `04b`:

```text
led.control 0xaa\r
led.status\r
```

UART B treats the UART A shell prompt as the end-of-transaction delimiter and
captures the complete response. The JTAG supervisor exposes at least:

| Command | Behavior |
|---|---|
| `link.get` | Schedule `led.status` through UART B |
| `link.set <value>` | Schedule `led.control <value>` through UART B |
| `link.result` | Report idle, pending, completed response, or timeout |
| `link.stats` | Show RX/TX byte counts, overruns, UART errors, requests, and timeouts |
| `link.reset` | Clear client parser state and diagnostics, then resynchronize on the UART A prompt |

Only one request may be in flight initially. A second request returns a clear
`busy` error. The response timeout must terminate the transaction without
hanging the firmware; reconnecting the jumpers followed by `link.reset` must
recover without reprogramming the FPGA.

---

### Task 1: Confirm IP and board interfaces before editing

**Files:**
- Inspect: Quartus 25.1 `altera_avalon_uart` component metadata
- Inspect: DE10-Nano user manual/schematic matching the physical board revision
- Record later in: `04c_niosv_dual_uart/doc/README.md`

**Interfaces:**
- Produces: verified UART component parameters, generated conduit naming, and a
  safe four-pin GPIO assignment.

- [ ] **Step 1: Confirm the installed UART IP name and parameters**

Use Quartus 25.1 component discovery or inspect its component Tcl. Confirm the
names and legal values corresponding to:

```text
component          altera_avalon_uart
clock              50 MHz
baud               115200, fixed
data bits           8
parity              none
stop bits           1
CTS/RTS             disabled
end-of-packet       disabled
RX synchronizer     enabled/default safe depth
```

Run a minimal `qsys-script` probe if necessary. Do not guess a parameter spelling
when the installed IP can report it.

- [ ] **Step 2: Confirm the software register contract**

Inspect the generated BSP header
`altera_avalon_uart_regs.h` and the installed Intel UART documentation. Confirm:

- RX data, TX data, status, and control register offsets.
- `RRDY`, `TRDY`, receive error, and interrupt-enable bit masks.
- How receive errors are acknowledged.
- Whether the selected core has hardware FIFOs and, if so, their configured
  depth. The software rings remain required even if small hardware FIFOs exist.

- [ ] **Step 3: Select four header signals**

Consult the official DE10-Nano manual for the exact board revision and select
four otherwise-unused FPGA GPIO signals on one expansion header:

| Logical signal | Direction at FPGA | Header signal | FPGA package pin |
|---|---|---|---|
| `uart_a_txd` | output | fill after verification | fill after verification |
| `uart_a_rxd` | input | fill after verification | fill after verification |
| `uart_b_txd` | output | fill after verification | fill after verification |
| `uart_b_rxd` | input | fill after verification | fill after verification |

Prefer adjacent signal positions when that makes the physical wiring easier, but
do not confuse connector position numbers, `GPIO_0[n]` signal indices, and FPGA
package pin names. Document all three where possible.

- [ ] **Step 4: Write down the physical wiring before power-up**

```text
UART A TX  ----------------  UART B RX
UART B TX  ----------------  UART A RX
```

Both signals are on the same powered board and already share ground. Any external
logic analyzer or USB-UART observer must also share board ground and must be
3.3 V compatible. Attach an observer as RX-only so it cannot contend with an
FPGA TX output.

---

### Task 2: Scaffold `04c_niosv_dual_uart` from `04b_niosv_led`

**Files:**
- Create: `04c_niosv_dual_uart/.gitignore`
- Create: `04c_niosv_dual_uart/hdl/de10_nano_top.vhd`
- Create: `04c_niosv_dual_uart/qsys/niosv_system.tcl`
- Create: `04c_niosv_dual_uart/quartus/Makefile`
- Create: `04c_niosv_dual_uart/quartus/de10_nano_project.tcl`
- Create: `04c_niosv_dual_uart/quartus/de10_nano_pin_assignments.tcl`
- Create: `04c_niosv_dual_uart/quartus/de10_nano.sdc`
- Create: `04c_niosv_dual_uart/software/app/Makefile`
- Create: `04c_niosv_dual_uart/software/app/led.hpp`
- Create: `04c_niosv_dual_uart/software/app/main.cpp`

**Interfaces:**
- Consumes: the known-good `04b_niosv_led` Nios V build structure.
- Produces: a renamed sibling that still builds before the dual-UART behavior is
  introduced.

- [ ] **Step 1: Copy only source-controlled `04b` inputs**

Do not copy generated `niosv_system.qsys`, `niosv_system.sopcinfo`,
`niosv_system_gen/`, `software/bsp/`, application `build/`, Quartus databases,
or bitstreams.

- [ ] **Step 2: Rename project-level identifiers**

Use:

```text
directory/project name: 04c_niosv_dual_uart
Quartus revision:        de10_nano
ELF name:                niosv_dual_uart.elf
```

Update Makefile variables, Quartus `project_new`, comments, and documentation.
Keep the generated Platform Designer system name `niosv_system` so the existing
build pattern and top-level component naming remain familiar.

- [ ] **Step 3: Verify the untouched baseline**

Run:

```bash
cd 04c_niosv_dual_uart/quartus
make qsys project compile QUARTUS_VERSION=25.1
make bsp app QUARTUS_VERSION=25.1
```

Expected: the renamed sibling produces a bitstream and
`software/app/build/niosv_dual_uart.elf` before UART additions.

- [ ] **Step 4: Commit the baseline scaffold**

```bash
git add 04c_niosv_dual_uart
git commit -m "feat(04c): scaffold Nios V dual UART project"
```

---

### Task 3: Add two physical UARTs to Platform Designer

**Files:**
- Modify: `04c_niosv_dual_uart/qsys/niosv_system.tcl`

**Interfaces:**
- Consumes: 50 MHz clock, system reset, Nios V data manager, and platform IRQ
  receiver.
- Produces: `UART_A_BASE`, `UART_A_IRQ`, `UART_B_BASE`, `UART_B_IRQ`, and two
  exported serial conduits in generated `system.h`/VHDL.

- [ ] **Step 1: Instantiate and configure UART A and UART B**

Use distinct instance names `uart_a` and `uart_b`. Apply the verified parameters
from Task 1 to both instances. Add comments describing A as the LED shell server
and B as the request client, while making clear that the hardware blocks are
identical.

- [ ] **Step 2: Connect both UARTs**

For each instance:

- Connect `clk_0.out_clk` to its clock sink.
- Connect `reset_bridge.out_reset` to its reset sink.
- Connect `niosv.data_manager` to its Avalon-MM agent/slave interface.
- Connect its IRQ sender to `niosv.platform_irq_rx`.
- Assign IRQ 1 to UART A and IRQ 2 to UART B; retain JTAG UART on IRQ 0.
- Assign proposed bases `0x00010200` and `0x00010220`, adjusted only if the
  generated span requires it.

- [ ] **Step 3: Export both serial conduits**

Export each UART's external connection as a top-level conduit. Generate the
system once and inspect the generated entity declaration rather than assuming
the exact RX/TX port suffixes.

- [ ] **Step 4: Regenerate and validate the hardware description**

Run:

```bash
cd 04c_niosv_dual_uart/quartus
make qsys QUARTUS_VERSION=25.1
```

Expected:

- Platform Designer reports no overlapping addresses or unconnected required
  interfaces.
- `niosv_system.sopcinfo` contains `uart_a` and `uart_b` at distinct bases.
- Generated `system.h` after the BSP step provides base and IRQ macros for both.

---

### Task 4: Route UART signals through the VHDL top level and GPIO pins

**Files:**
- Modify: `04c_niosv_dual_uart/hdl/de10_nano_top.vhd`
- Modify: `04c_niosv_dual_uart/quartus/de10_nano_pin_assignments.tcl`

**Interfaces:**
- Consumes: generated UART A/B serial conduit ports.
- Produces: four single-ended 3.3 V top-level FPGA pins.

- [ ] **Step 1: Add explicit top-level ports**

Add `uart_a_rxd`, `uart_a_txd`, `uart_b_rxd`, and `uart_b_txd` to the entity.
Declare the generated `niosv_system` component ports using names confirmed from
Task 3 and map them directly. Do not invert, gate, or internally loop the signals.

- [ ] **Step 2: Add verified location and I/O-standard assignments**

Set all four signals to `3.3-V LVTTL` and use only the package pins verified in
Task 1. Add a short pin-assignment comment containing the header connector name
and `GPIO_x[n]` label for each signal.

- [ ] **Step 3: Compile and inspect pin reports before installing jumpers**

Run:

```bash
cd 04c_niosv_dual_uart/quartus
make project compile QUARTUS_VERSION=25.1
```

Expected:

- No multiple-driver, reserved-pin, illegal-I/O-bank, or missing-location errors.
- The Fitter pin report shows UART TX signals as outputs and RX signals as inputs.
- Timing analysis has no unconstrained internal clock introduced by the UARTs.

- [ ] **Step 4: Commit the hardware path**

```bash
git add 04c_niosv_dual_uart/qsys \
        04c_niosv_dual_uart/hdl \
        04c_niosv_dual_uart/quartus
git commit -m "feat(04c): add dual physical UART hardware"
```

---

### Task 5: Implement the reusable interrupt-buffered Avalon UART transport

**Files:**
- Create: `common/software/niosv_shell/spsc_ring.hpp`
- Create: `common/software/niosv_shell/avalon_uart.hpp`
- Create: `04c_niosv_dual_uart/software/tests/test_spsc_ring.cpp`
- Create or modify: `04c_niosv_dual_uart/software/tests/Makefile`

**Interfaces:**
- Consumes: BSP base address and IRQ macros for one Avalon UART.
- Produces the same shell transport surface used by `JtagUart`:

```cpp
void begin();
bool read(char &value);
void write(char value);
void write(const char *text);
```

- Also produces diagnostic accessors for byte counts, software-ring overruns,
  and hardware parity/framing/break/receive-overrun errors.

- [ ] **Step 1: Implement a fixed-capacity single-producer/single-consumer ring**

Requirements:

- No allocation, exceptions, RTTI, C library, or locks in normal push/pop paths.
- Capacity is a compile-time constant and leaves one slot unused or tracks count
  explicitly so full and empty cannot be confused.
- RX usage is ISR producer/main-loop consumer.
- TX usage is main-loop producer/ISR consumer.
- Index publication is safe for interrupt/main-loop concurrency on RV32.
- Capacities are powers of two if masking is used.

Start with at least 256 RX bytes and 512 TX bytes per UART. Confirm final static
RAM cost against the 64 KB on-chip memory in the ELF size report.

- [ ] **Step 2: Unit-test ring wraparound and overflow behavior on the host**

Test empty/full detection, ordering, wraparound, repeated fill/drain cycles, and
the documented overflow policy. Build with strict warnings:

```bash
cd 04c_niosv_dual_uart/software/tests
make test
```

- [ ] **Step 3: Implement `AvalonUart` using verified register definitions**

Prefer the BSP's symbolic register macros and bit masks where they can be used
without coupling application code to a HAL-owned buffered driver. If a thin
typed MMIO wrapper is required, keep verified register details isolated in this
header.

Initialization order must be explicit:

1. Disable the UART's interrupt-enable bits.
2. Clear/drain stale RX and error state according to the core contract.
3. Reset software rings and counters.
4. Replace/register the HAL ISR using the generated controller and IRQ IDs.
5. Enable receive-ready and receive-error interrupts.
6. Enable transmit-ready interrupt only when TX data is queued.

- [ ] **Step 4: Keep the ISR bounded**

The ISR must:

- Read status once per service iteration.
- Move ready RX bytes into the RX ring.
- Move queued TX bytes into the hardware when transmit-ready.
- Disable transmit-ready interrupt when the TX ring becomes empty to prevent an
  interrupt storm.
- Count and acknowledge hardware errors.
- Never parse commands, format text, access LEDs, write JTAG output, or wait for
  ring space.

- [ ] **Step 5: Make foreground writes safe under backpressure**

`CommandShell::write()` has no failure return. If the TX software ring becomes
full, foreground `write(char)` may wait for ISR-driven progress, but interrupts
must remain enabled so both physical receivers continue to drain. Document this
behavior and ensure the ISR never calls the blocking API.

- [ ] **Step 6: Verify ownership against the generated HAL**

Ensure the application and the generated interrupt-driven HAL UART driver cannot
both consume the same registers. Keep JTAG UART selected as BSP stdin/stdout, and
have `AvalonUart::begin()` explicitly take ownership of UART A/B by replacing
their handlers/control state. Do not use `printf`, `read()`, or `write()` on the
physical UART device nodes after ownership transfers.

- [ ] **Step 7: Commit the reusable transport**

```bash
git add common/software/niosv_shell \
        04c_niosv_dual_uart/software/tests
git commit -m "feat(common): add interrupt-buffered Avalon UART transport"
```

---

### Task 6: Reuse the LED shell on UART A

**Files:**
- Modify: `04c_niosv_dual_uart/software/app/main.cpp`
- Modify as needed: `04c_niosv_dual_uart/software/app/led.hpp`

**Interfaces:**
- Consumes: `AvalonUart<UART_A_BASE, ...>` and existing `CommandShell`.
- Produces: the `04b` LED command set over physical UART A.

- [ ] **Step 1: Preserve the existing LED behavior**

Keep `led.status`, `led.control`, `led.on`, `led.off`, `led.toggle`, and
`led.cycle`. Keep symbolic `LED_PIO_BASE` access and the existing validation
rules.

- [ ] **Step 2: Bind the server shell to UART A**

The exact template signature may change during Task 5, but all hardware values
come from `system.h`, conceptually:

```cpp
using ServerUart = cvsoc::AvalonUart<
    UART_A_BASE,
    UART_A_IRQ_INTERRUPT_CONTROLLER_ID,
    UART_A_IRQ>;
using ServerShell = cvsoc::CommandShell<ServerUart>;
```

Instantiate only one interactive shell on the physical link: UART A. Its output
must travel through the TX ring and across the physical jumper to UART B.

- [ ] **Step 3: Define the boot synchronization rule**

UART A emits the normal shell banner and prompt at startup. UART B begins in a
`Synchronizing` state and discards/captures input until it recognizes the full
prompt token `niosv> `. It must tolerate the prompt arriving across multiple
poll calls.

---

### Task 7: Implement the UART B asynchronous command client

**Files:**
- Create: `04c_niosv_dual_uart/software/app/link_client.hpp`
- Create: `04c_niosv_dual_uart/software/tests/test_link_client.cpp`
- Modify: `04c_niosv_dual_uart/software/tests/Makefile`

**Interfaces:**
- Consumes: a UART-like transport and textual UART A shell responses.
- Produces: idle/synchronizing/pending/completed/timed-out state, captured
  response, parsed LED status when present, and link counters.

- [ ] **Step 1: Define an explicit state machine**

At minimum:

```text
Synchronizing -> Idle -> Sending -> WaitingForPrompt -> Complete -> Idle
                              \-> TimedOut -----------/
```

State changes occur from `poll()` calls. No state method may wait for a complete
command or response.

- [ ] **Step 2: Bound every buffer and transaction**

- Support one request in flight.
- Reject a request longer than the command buffer.
- Cap the captured transcript and mark truncation without overflowing.
- Match the prompt incrementally across input chunks.
- Ignore stale banner bytes while synchronizing.
- Reject/flag malformed `led.status` values.
- Time out a missing response with a documented, monotonic deadline source.

If no reliable HAL timebase is configured, add a small dedicated timebase or use
the Nios V machine timer deliberately; do not call an arbitrary loop count
"milliseconds."

- [ ] **Step 3: Format only known-safe requests**

Provide typed operations such as `request_led_status()` and
`request_led_control(uint8_t)`. Use fixed buffers and bounded formatting. A raw
command operation is optional and must not be required for acceptance.

- [ ] **Step 4: Host-test fragmented and erroneous transcripts**

Feed a fake UART transport with:

- Banner and prompt in one chunk and one byte at a time.
- Echoed `led.status` followed by `led.status = 0xaa` and a prompt.
- `OK` response to `led.control`.
- CRLF split across polls.
- Prompt split at every possible boundary.
- Unknown-command and malformed-status responses.
- Oversized response, missing prompt, timeout, reset, and later recovery.
- A busy request while another request is pending.

Expected: no allocation, no out-of-bounds access, deterministic states, and
correct final result/counters.

- [ ] **Step 5: Commit the client**

```bash
git add 04c_niosv_dual_uart/software/app/link_client.hpp \
        04c_niosv_dual_uart/software/tests
git commit -m "feat(04c): add asynchronous UART command client"
```

---

### Task 8: Add the JTAG supervisor shell and integrate the main loop

**Files:**
- Create: `04c_niosv_dual_uart/software/app/link_commands.hpp`
- Modify: `04c_niosv_dual_uart/software/app/main.cpp`
- Modify if necessary: `common/software/niosv_shell/command_shell.hpp`
- Add tests if the common shell changes.

**Interfaces:**
- Consumes: `JtagUart`, `LinkClient`, UART A server shell, and LED commands.
- Produces: interactive JTAG commands for scheduling and inspecting link work.

- [ ] **Step 1: Implement the supervisor commands**

Add `link.get`, `link.set`, `link.result`, `link.stats`, and `link.reset` with the
semantics defined above. Validate `link.set` as an 8-bit value using the existing
shell parsing helpers.

`link.get` and `link.set` must enqueue/schedule and return immediately. They must
not spin waiting for UART B because UART A's server shell needs the same main loop
to consume the request and produce the response.

- [ ] **Step 2: Keep the two shell types separate**

Use one `CommandShell<JtagUart<...>>` for supervisor commands and one
`CommandShell<AvalonUart<...>>` for LED server commands. Command handler aliases
are transport-specific in the current template, so give each command group its
correct shell type instead of casting between them.

- [ ] **Step 3: Integrate a cooperative main loop**

The loop should repeatedly service all foreground state machines, for example:

```cpp
while (true) {
    supervisor_shell.poll();
    link_client.poll(now());
    server_shell.poll();
    link_client.poll(now());
    led_commands.tick();
}
```

The second client poll reduces response latency but is not a correctness
requirement because UART interrupts capture bytes independently.

- [ ] **Step 4: Handle asynchronous completion without corrupting input**

The simplest accepted interface is for `link.result` to retrieve completion, so
the client need not print while the user is editing a JTAG command. If automatic
completion notifications are added, extend `CommandShell` with a tested redraw
operation that preserves the partially entered line. Do not print an unsolicited
prompt directly from application code.

- [ ] **Step 5: Initialize in an ownership-safe order**

1. Construct both physical transports and command/client objects.
2. Call `begin()` on UART A and UART B to install their interrupt handlers.
3. Start the UART A server shell so it queues its banner/prompt.
4. Start the JTAG supervisor transport and shell.
5. Enter the cooperative main loop and let UART B synchronize on UART A's prompt.

- [ ] **Step 6: Build and inspect size**

Run:

```bash
cd 04c_niosv_dual_uart/quartus
make bsp app QUARTUS_VERSION=25.1
```

Expected: zero warnings from project-owned C++, link succeeds within 64 KB RAM,
and the ELF contains distinct base/IRQ bindings for UART A and UART B.

- [ ] **Step 7: Commit firmware integration**

```bash
git add 04c_niosv_dual_uart/software/app \
        common/software/niosv_shell/command_shell.hpp
git commit -m "feat(04c): integrate dual UART LED command loop"
```

Omit the common header from the commit if no common-shell change was needed.

---

### Task 9: Add build targets and hardware diagnostics

**Files:**
- Modify: `04c_niosv_dual_uart/quartus/Makefile`
- Modify: `04c_niosv_dual_uart/software/app/Makefile`
- Modify: `04c_niosv_dual_uart/.gitignore`

**Interfaces:**
- Produces: reproducible hardware/software builds, JTAG supervisor startup, and
  host-only unit-test entry points.

- [ ] **Step 1: Preserve standard Nios V targets**

Keep `qsys`, `project`, `compile`, `bsp`, `app`, `program-sof`, `download-elf`,
`terminal`, and `clean` consistent with `04b`. Use ELF name
`niosv_dual_uart.elf`.

- [ ] **Step 2: Add a host test target**

Add a target such as:

```bash
make test-host
```

It must compile and run ring/client tests without Quartus or the RiscFree
toolchain. Keep host test objects outside source directories or ignore them.

- [ ] **Step 3: Make clean remove only generated outputs**

Cover Platform Designer generation, Quartus databases/reports, BSP output,
CMake application output, and host-test output. Preserve every hand-written
source and documentation file.

- [ ] **Step 4: Verify clean rebuild**

```bash
cd 04c_niosv_dual_uart/quartus
make clean QUARTUS_VERSION=25.1
make test-host
make all QUARTUS_VERSION=25.1
```

Expected: tests pass, hardware compiles, BSP/app compile, and no untracked
generated files remain outside ignored paths.

---

### Task 10: Perform staged on-board verification

**Files:**
- Record results in: `04c_niosv_dual_uart/doc/README.md`

**Interfaces:**
- Consumes: programmed SOF, downloaded ELF, JTAG terminal, and two crossed jumper
  wires.
- Produces: evidence that bytes traverse the FPGA pins and external wires rather
  than an internal connection.

- [ ] **Step 1: Program and run with no jumpers installed**

```bash
cd 04c_niosv_dual_uart/quartus
make program-sof
make download-elf
make terminal
```

Expected:

- JTAG supervisor starts normally.
- `link.get` becomes pending and then times out.
- `link.stats` reports no unexplained receive bytes.
- The application and LED cycling continue; no deadlock occurs.

- [ ] **Step 2: Power down before installing jumpers**

Install only the two verified crossed signal wires:

```text
A_TX -> B_RX
B_TX -> A_RX
```

Visually verify that no TX-to-TX or signal-to-power connection exists before
powering the board again.

- [ ] **Step 3: Verify synchronization**

After programming/downloading, confirm UART B observes UART A's startup banner
and reaches `Idle`. `link.stats` should show bytes in both directions and no
software/hardware overruns.

- [ ] **Step 4: Verify LED status and control**

From the JTAG supervisor:

```text
link.set 0xaa
link.result
link.get
link.result
```

Expected:

- Physical LEDs display `0xaa` after the control transaction.
- The control response contains `OK`.
- The status response contains `led.status = 0xaa`.
- UART A TX/RX and UART B TX/RX counters all increase.
- Error and overflow counters remain zero.

Repeat with `0x00`, `0xff`, `0x55`, and at least 100 alternating status/control
transactions.

- [ ] **Step 5: Prove each physical direction is required**

Power down before changing wires.

- Remove `A_TX -> B_RX` only: requests can leave B, but replies cannot complete;
  expect timeout.
- Restore it and remove `B_TX -> A_RX` only: A receives no command; expect timeout.
- Restore both: `link.reset` followed by a request must recover.

This staged test distinguishes a real external round trip from an accidental
internal loopback.

- [ ] **Step 6: Verify malformed and recovery paths**

Exercise busy rejection, missing prompt timeout, parser reset, response
truncation if test injection exists, and reconnect recovery. Confirm LED cycling
and JTAG interaction remain responsive throughout.

- [ ] **Step 7: Inspect electrical signaling if equipment is available**

Optionally observe each TX line with a 3.3 V logic analyzer or an RX-only
USB-UART adapter. Confirm idle-high UART, approximately 115200 baud, and 8N1
framing. Observation is supplemental; do not make external equipment mandatory.

---

### Task 11: Write the learning documentation

**Files:**
- Create: `04c_niosv_dual_uart/doc/README.md`
- Create: `04c_niosv_dual_uart/doc/DUAL_UART_TUTORIAL.md`
- Modify if the repository index requires it: `README.md`

**Interfaces:**
- Produces: a reproducible wiring/build/use guide and a conceptual explanation
  of why the implementation uses buffering and asynchronous state machines.

- [ ] **Step 1: Document architecture and final memory map**

Include a Mermaid diagram showing JTAG supervisor, UART B client, physical
jumpers, UART A server, command shell, and LED PIO. Use the final generated base
addresses, spans, and IRQs.

- [ ] **Step 2: Document exact header wiring**

Include:

- Board revision used for verification.
- Header connector name and physical position.
- `GPIO_x[n]` signal name.
- FPGA package pin.
- Project top-level signal.
- Direction.
- A clear crossed-wire diagram.
- 3.3 V warning and power-off-before-rewiring instruction.

- [ ] **Step 3: Document the complete workflow**

```bash
cd 04c_niosv_dual_uart/quartus
make test-host
make all QUARTUS_VERSION=25.1
make program-sof
make download-elf
make terminal
```

Explain that `picocom` is not part of the normal two-UART loop because both
endpoints live in the FPGA. Explain safe RX-only observation separately.

- [ ] **Step 4: Teach the important failure mode**

Explain why naïvely polling two classic UARTs from one loop can overrun the
inactive receiver while the shell is synchronously formatting/transmitting a
response. Relate hardware status bits, interrupt handlers, software rings, and
the foreground parser without assuming prior interrupt knowledge.

- [ ] **Step 5: Teach protocol roles**

Clarify that a UART peripheral does not inherently "control" or "report" LEDs.
Firmware assigns UART B the request-client role and UART A the LED-shell-server
role. Show request, echoed command, result, and prompt framing for both
`link.set` and `link.get`.

- [ ] **Step 6: Add troubleshooting**

Cover at least:

- Link never synchronizes: swapped/missing jumper or wrong pin assignment.
- Continuous framing errors: baud/clock mismatch or signal integrity.
- No bytes either direction: ELF not running, reset asserted, or wrong header.
- One-way success: one crossed jumper missing.
- RX overflow: ISR ownership/enable problem or undersized buffer.
- JTAG works but physical UART does not: expected separation of transports.
- Onboard USB-UART opens in `picocom` but sees nothing: it belongs to the HPS
  path, not these FPGA UART pins.

- [ ] **Step 7: Link authoritative references**

Reference the installed Intel UART IP documentation used for the register and
interrupt contract and the official Terasic DE10-Nano document page used for
the chosen board-revision pinout.

---

### Task 12: Final regression and handoff

**Files:**
- Verify all files changed by Tasks 1–11.

- [ ] **Step 1: Run host tests and formatting checks**

```bash
cd 04c_niosv_dual_uart/quartus
make test-host
cd ../..
git diff --check
```

- [ ] **Step 2: Run the full clean build**

```bash
cd 04c_niosv_dual_uart/quartus
make clean QUARTUS_VERSION=25.1
make all QUARTUS_VERSION=25.1
```

- [ ] **Step 3: Regression-check `04b`**

At minimum, rebuild its application after any shared shell changes:

```bash
cd ../../04b_niosv_led/quartus
make bsp app QUARTUS_VERSION=25.1
```

If shared UART/shell headers are included only by `04c`, still run host tests and
confirm `04b` source compatibility.

- [ ] **Step 4: Repeat the hardware acceptance sequence**

Confirm:

- JTAG supervisor remains responsive.
- No-jumper requests time out rather than hang.
- Both jumpers produce successful `link.set` and `link.get` transactions.
- LED readback matches the requested value.
- Removing either direction breaks the round trip.
- Restoring wires plus `link.reset` recovers.
- Error and overflow counters stay zero during the 100-transaction run.

- [ ] **Step 5: Inspect repository state**

```bash
git status --short
git diff --stat
```

Expected: only intended source/documentation changes appear; generated Qsys,
BSP, CMake, Quartus, ELF, and test outputs are ignored or cleaned.

- [ ] **Step 6: Commit documentation and final integration**

```bash
git add 04c_niosv_dual_uart README.md
git commit -m "docs(04c): add dual physical UART learning project"
```

Omit `README.md` if no repository-level index change is necessary.

## Acceptance criteria

- `04c_niosv_dual_uart` is a 25.1-only sibling and `04b_niosv_led` remains
  functional.
- Quartus generates and compiles a Nios V system containing JTAG UART plus two
  distinct physical UART instances.
- Four verified 3.3 V GPIO pins expose A/B RX/TX, with external crossed jumpers
  providing the only A-to-B connection.
- UART A runs the reused LED `CommandShell`; UART B runs a non-blocking client;
  JTAG UART runs supervisor commands.
- Both physical UARTs use interrupt-backed fixed buffers and report transport
  diagnostics.
- `link.set` changes LEDs, and `link.get` returns the same status through a real
  two-wire round trip.
- Removing either jumper causes a bounded timeout, and restoring wiring permits
  software recovery without FPGA reprogramming.
- Host tests cover ring-buffer boundaries and fragmented/error client responses.
- A 100-transaction hardware run completes with no UART error or overflow count.
- Documentation records exact board-revision pinout, safe wiring, build/run
  commands, protocol behavior, and troubleshooting.

## Authoritative implementation references

- Existing reusable shell and JTAG transport:
  `common/software/niosv_shell/`
- Existing project baseline: `04b_niosv_led/`
- Existing Nios V HAL interrupt example: `06b_niosv_interrupts/software/app/main.c`
- Intel Embedded Peripherals IP User Guide, UART Core chapter:
  <https://www.intel.com/content/www/us/en/docs/programmable/683130/24-3/functional-description-33824.html>
- Official Terasic DE10-Nano documents and board-revision manuals:
  <https://www.terasic.com.tw/cgi-bin/page/archive.pl?CategoryNo=205&Language=English&No=1046&PartNo=4>
