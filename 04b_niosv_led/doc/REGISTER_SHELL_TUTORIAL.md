# Building an Interactive Register Shell with Embedded C++

This tutorial explains how the `04b_niosv_led` application lets you type
commands such as:

```text
niosv> led.status
led.status = 0xaa

niosv> led.control = 0x55
OK
```

It is written for developers who know some C but are new to embedded C++.
The important idea is that the terminal does not understand C++ objects. It
only sends and receives characters. The firmware connects a command name such
as `led.status` to a C++ function that accesses the hardware.

## 1. The complete path

When you enter `led.status`, the request passes through several layers:

```text
Your keyboard
    |
    v
juart-terminal on the development computer
    |
    | characters transported through USB-Blaster/JTAG
    v
JTAG UART peripheral inside the FPGA
    |
    v
CommandShell in the Nios V firmware
    |
    | command-table lookup for "led.status"
    v
LedCommands::status()
    |
    v
Led::status()
    |
    | memory-mapped read
    v
LED PIO hardware register
```

The response travels through the same layers in the opposite direction.

There are two computers involved:

- The **host** is the Ubuntu workstation running `juart-terminal`.
- The **target** is the Nios V processor running inside the FPGA.

`juart-terminal` is only a character transport. It does not know that an LED
peripheral exists, where its registers are, or what `led.status` means. All of
that knowledge is compiled into the target firmware.

## 2. Memory-mapped hardware

Nios V accesses peripherals through memory-mapped I/O. A peripheral register
appears at an address in the processor's address space. Reading from that
address reads the hardware register; writing to it changes the hardware.

In this project, Platform Designer assigns an address to `led_pio`. When the
BSP is generated, it writes the address into the generated `system.h` file:

```cpp
#define LED_PIO_BASE 0x00010010
```

Application code should use `LED_PIO_BASE`, not repeat `0x00010010`. If the
Platform Designer address changes, regenerating the BSP updates `system.h`, and
the application follows the new address automatically.

The generated file also provides:

```cpp
#define JTAG_UART_BASE 0x00010100
```

These definitions are included in the application with:

```cpp
#include "system.h"
```

## 3. The `Led` class template

The LED abstraction is defined in [`led.hpp`](../software/app/led.hpp):

```cpp
template <std::uintptr_t Base, unsigned Width>
class Led {
    // ...
};
```

This is a C++ **class template**. A template is a recipe for creating a class.
It receives two compile-time values:

- `Base`: the peripheral's memory-mapped base address.
- `Width`: the number of LED bits implemented by the peripheral.

The application turns the template into a concrete type:

```cpp
using BoardLed = Led<LED_PIO_BASE, 8u>;
```

Read this as:

> `BoardLed` is an `Led` device located at the generated `LED_PIO_BASE`, with
> eight output bits.

`using` creates a type alias. It does not create an object. An actual object is
created later inside `LedCommands`:

```cpp
BoardLed led_;
```

The distinction is:

```cpp
using BoardLed = Led<LED_PIO_BASE, 8u>; // Defines a type.
BoardLed led_;                          // Creates an object of that type.
```

### Reading the register

The public `status()` method reads the current PIO data register:

```cpp
std::uint32_t status() const
{
    return data_register() & mask();
}
```

The mask ensures that only the configured number of bits is returned. For an
eight-bit LED device, the mask is `0xff`.

### Writing the register

The `control()` method writes a new value:

```cpp
void control(std::uint32_t value)
{
    data_register() = value & mask();
}
```

Again, the mask prevents bits outside the LED width from being written.

### Converting an address into a register

The private `data_register()` function is where C++ meets the hardware:

```cpp
static volatile std::uint32_t &data_register()
{
    return *reinterpret_cast<volatile std::uint32_t *>(Base);
}
```

Breaking this down:

1. `Base` is an integer containing the peripheral address.
2. `reinterpret_cast<volatile std::uint32_t *>(Base)` treats that integer as a
   pointer to a 32-bit hardware register.
3. The leading `*` dereferences the pointer.
4. The function returns a reference to the register.

The word `volatile` is essential for memory-mapped I/O. It tells the compiler:

> Every read and write matters because the value can change outside normal
> program execution.

Without `volatile`, an optimizer could reuse an earlier read or remove a write
that looks redundant. That is legal for ordinary RAM but wrong for hardware
registers.

### Convenience operations

The class also provides bit-oriented methods:

```cpp
led_.on(0);      // Set bit 0.
led_.off(3);     // Clear bit 3.
led_.toggle(7);  // Invert bit 7.
```

Each method checks that the index is smaller than `Width`. It then performs a
read-modify-write operation:

```text
read current value -> change one bit -> write new value
```

For this output-only PIO, `status()` and `control()` access the same data
register. The names describe how the application uses that register; they do
not refer to two separate hardware registers.

## 4. The JTAG UART transport

The reusable JTAG UART class is in
[`jtag_uart.hpp`](../../common/software/niosv_shell/jtag_uart.hpp):

```cpp
using BoardUart = cvsoc::JtagUart<JTAG_UART_BASE>;
```

Like `Led`, it receives its base address at compile time. It provides three
small operations:

```cpp
bool read(char &value);
void write(char value);
void write(const char *text);
```

`read()` checks the JTAG UART's `RVALID` bit. If a character is ready, it
returns the character and `true`; otherwise it returns `false` without
blocking.

`write()` waits until the transmit FIFO has space and then writes one
character. The overload taking `const char *` repeats that operation until it
reaches the string's terminating `\0` byte.

### Why `uart.begin()` disables interrupts

The generated HAL initializes its own interrupt-driven JTAG UART driver before
`main()` runs. This application deliberately accesses the UART with a small
polled driver instead. If both readers remained active, the HAL interrupt
handler and the command shell could each consume part of the input.

Therefore, `uart.begin()` disables the JTAG UART read/write interrupt-enable
bits before the shell starts polling:

```cpp
uart.begin();
shell.begin();
```

The shell then has exclusive ownership of the UART receive stream.

## 5. The command table

The terminal learns nothing automatically from the C++ class. Instead, the
firmware explicitly registers command strings in
[`main.cpp`](../software/app/main.cpp):

```cpp
const Shell::Command commands[] = {
    {"led.status", "led.status",
     "Read the current 8-bit LED output.",
     LedCommands::status, &led_commands},

    {"led.control", "led.control [=] <value>",
     "Write the LED output and pause cycling.",
     LedCommands::control, &led_commands},

    // More commands follow.
};
```

Each entry contains five fields:

| Field | Example | Purpose |
|---|---|---|
| Name | `"led.status"` | Text matched against user input |
| Usage | `"led.status"` | Syntax displayed by `help` |
| Description | `"Read the current..."` | Explanation displayed by `help` |
| Handler | `LedCommands::status` | Function called after a match |
| Context | `&led_commands` | Object passed to the handler |

The table is passed to the shell:

```cpp
Shell shell(uart, commands, sizeof(commands) / sizeof(commands[0]));
```

This is how the shell knows that `led.status` exists.

The dot is not special C++ syntax here. The parser treats `led.status` as one
ordinary string. We use the dot only to create readable, object-like command
names.

## 6. Function pointers and context

A command entry stores a **function pointer**. A function pointer is the
address of a function that can be called later.

All command handlers have the same shape:

```cpp
void handler(void *context, Shell &shell, const char *arguments);
```

The shell is reusable and does not know about `LedCommands`. It therefore
stores the object as a generic `void *`. The LED handler converts it back:

```cpp
static LedCommands &instance(void *context)
{
    return *static_cast<LedCommands *>(context);
}
```

The status handler can then access the real object:

```cpp
static void status(void *context, Shell &shell, const char *arguments)
{
    auto &self = instance(context);

    shell.write("led.status = ");
    shell.write_hex(self.led_.status(), 2u);
    shell.write("\r\n");
}
```

This explicit function-pointer design avoids dynamic allocation, inheritance,
virtual functions, and a large runtime framework.

## 7. Parsing a command line

The reusable parser is in
[`command_shell.hpp`](../../common/software/niosv_shell/command_shell.hpp).

It owns a fixed-size line buffer:

```cpp
char line_[80]{};
```

A fixed buffer is common in small embedded systems because:

- Its memory consumption is known at compile time.
- It does not require a heap.
- It cannot grow until the system runs out of memory.

`shell.poll()` repeatedly checks for received UART characters:

```cpp
void poll()
{
    char value;
    while (uart_.read(value))
        accept(value);
}
```

`accept()` handles normal characters, Enter, Backspace, and line-length
limits. When Enter arrives, `execute()` separates the command name from its
arguments.

For example:

```text
led.control = 0xaa
```

becomes approximately:

```text
name      -> "led.control"
arguments -> "0xaa"
```

Both of these forms are accepted:

```text
led.control 0xaa
led.control = 0xaa
```

The parser then searches the command table:

```cpp
for (std::size_t i = 0; i < command_count_; ++i) {
    if (equals(name, commands_[i].name)) {
        commands_[i].handler(
            commands_[i].context,
            *this,
            arguments
        );
        return;
    }
}
```

If no name matches, the shell prints an unknown-command error.

## 8. Following `led.status` end to end

Consider this input:

```text
niosv> led.status
```

The complete sequence is:

1. `juart-terminal` sends the characters through JTAG.
2. The FPGA's JTAG UART stores them in its receive FIFO.
3. `shell.poll()` reads the characters one by one.
4. `accept()` stores them in `line_` and echoes them.
5. Enter causes `execute()` to run.
6. `execute()` extracts the name `"led.status"`.
7. The command-table search finds the matching entry.
8. The entry calls `LedCommands::status()`.
9. The handler calls `self.led_.status()`.
10. `Led::status()` reads the PIO data register at `LED_PIO_BASE`.
11. The handler formats the value as hexadecimal.
12. `JtagUart::write()` sends the response back to the host.

Nothing in this path uses C++ reflection or automatic object discovery. The
connection is explicit and therefore small, predictable, and easy to audit.

## 9. The main loop

The application does not use an operating system. Its `main()` function runs a
continuous loop:

```cpp
while (true) {
    shell.poll();
    led_commands.tick();
}
```

Each iteration:

1. Processes any available terminal characters.
2. Advances the automatic LED pattern when enough ticks have passed.

Commands that manually modify the LEDs turn cycling off so a requested value
is not immediately overwritten:

```text
led.control 0xaa
led.on 0
led.off 3
led.toggle 7
```

Cycling can be resumed explicitly:

```text
led.cycle on
```

## 10. Adding another device

Suppose a future Platform Designer system contains a two-bit button PIO and the
generated `system.h` defines `BUTTON_PIO_BASE`.

First, define a typed hardware class:

```cpp
template <std::uintptr_t Base, unsigned Width>
class Buttons {
public:
    std::uint32_t status() const
    {
        return *reinterpret_cast<volatile std::uint32_t *>(Base) & mask();
    }

private:
    static constexpr std::uint32_t mask()
    {
        return 0xffffffffu >> (32u - Width);
    }
};

using BoardButtons = Buttons<BUTTON_PIO_BASE, 2u>;
```

Next, create a command owner and handler:

```cpp
class ButtonCommands {
public:
    static void status(void *context, Shell &shell, const char *arguments)
    {
        auto &self = *static_cast<ButtonCommands *>(context);
        shell.write("button.status = ");
        shell.write_hex(self.buttons_.status(), 1u);
        shell.write("\r\n");
    }

private:
    BoardButtons buttons_;
};
```

Finally, register the command:

```cpp
ButtonCommands button_commands;

const Shell::Command commands[] = {
    // Existing LED entries...
    {"button.status", "button.status", "Read both push buttons.",
     ButtonCommands::status, &button_commands},
};
```

The shell now knows `button.status` because that name appears in the command
table. It still does not need a numeric address from the user.

## 11. Why use C++ for this?

The same hardware accesses could be written in C. C++ helps organize the code:

- `Led` owns the rules for accessing an LED PIO.
- `LedCommands` owns terminal behavior for that device.
- `CommandShell` owns input editing, parsing, help, and dispatch.
- `JtagUart` owns the transport details.

Templates let addresses and widths remain compile-time constants. The compiler
can inline these small functions, so the abstraction does not require a
runtime object lookup.

This project intentionally avoids expensive or unpredictable features:

- No dynamic allocation with `new` or `delete`.
- No C++ streams such as `std::cout`.
- No runtime type information.
- No exceptions in application logic.
- No dynamically growing strings or containers.

The result keeps the useful structure of C++ while remaining suitable for a
small soft-core system.

## 12. Building and using the shell

From the Quartus directory:

```bash
cd 04b_niosv_led/quartus
make all
make program-sof
make download-elf
make terminal
```

Then try:

```text
niosv> help led
niosv> led.cycle off
niosv> led.control = 0xaa
niosv> led.status
niosv> led.on 0
niosv> led.toggle 7
niosv> led.cycle on
```

Use Ctrl-C on the host to close `juart-terminal`. Closing the terminal does not
stop the Nios V application; it continues running on the FPGA.

## 13. What is automatic and what is explicit?

The current design automates address handling but keeps device behavior
explicit.

Automatic:

- Platform Designer defines the hardware memory map.
- BSP generation creates `LED_PIO_BASE` and `JTAG_UART_BASE` in `system.h`.
- C++ templates bind those addresses into device types at compile time.
- `help` output is generated from the command table.

Explicit:

- The firmware creates the `BoardLed` type.
- The firmware defines what `status`, `control`, `on`, and `off` mean.
- The firmware registers each public terminal command.

It would be possible to generate type aliases and command-table entries by
parsing the Platform Designer `.sopcinfo` file. However, hardware metadata
cannot decide all application semantics. For example, it cannot know whether a
PIO should be presented as LEDs, relays, chip-selects, or another concept.
Keeping semantic device classes explicit makes that decision visible in the
source code.
