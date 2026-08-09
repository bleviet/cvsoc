#include <cstddef>
#include <cstdint>

#include "system.h"

#include "../../../common/software/niosv_shell/command_shell.hpp"
#include "../../../common/software/niosv_shell/jtag_uart.hpp"
#include "led.hpp"

using BoardLed = Led<LED_PIO_BASE, 8u>;
using BoardUart = cvsoc::JtagUart<JTAG_UART_BASE>;
using Shell = cvsoc::CommandShell<BoardUart>;

namespace {

constexpr std::uint8_t kPatterns[] = {
    0x01, 0x03, 0x07, 0x0f, 0x1f, 0x3f, 0x7f, 0xff,
    0x7f, 0x3f, 0x1f, 0x0f, 0x07, 0x03, 0x01, 0x00,
    0xaa, 0x55, 0xff, 0x00,
};

class LedCommands {
public:
    void tick()
    {
        if (!cycling_ || ++ticks_ < kTicksPerPattern)
            return;

        ticks_ = 0u;
        led_.control(kPatterns[pattern_]);
        pattern_ = (pattern_ + 1u) % (sizeof(kPatterns) / sizeof(kPatterns[0]));
    }

    static void status(void *context, Shell &shell, const char *arguments)
    {
        auto &self = instance(context);
        if (!Shell::no_arguments(arguments)) {
            shell.error("usage: led.status");
            return;
        }

        shell.write("led.status = ");
        shell.write_hex(self.led_.status(), 2u);
        shell.write("\r\n");
    }

    static void control(void *context, Shell &shell, const char *arguments)
    {
        auto &self = instance(context);
        if (Shell::no_arguments(arguments)) {
            status(context, shell, arguments);
            return;
        }

        std::uint32_t value;
        if (!Shell::parse_u32(arguments, value) || value > 0xffu) {
            shell.error("expected an 8-bit value (for example 0xaa)");
            return;
        }

        self.cycling_ = false;
        self.led_.control(value);
        shell.ok();
    }

    static void on(void *context, Shell &shell, const char *arguments)
    {
        update_bit(context, shell, arguments, BitOperation::On);
    }

    static void off(void *context, Shell &shell, const char *arguments)
    {
        update_bit(context, shell, arguments, BitOperation::Off);
    }

    static void toggle(void *context, Shell &shell, const char *arguments)
    {
        update_bit(context, shell, arguments, BitOperation::Toggle);
    }

    static void cycle(void *context, Shell &shell, const char *arguments)
    {
        auto &self = instance(context);
        if (Shell::no_arguments(arguments)) {
            shell.write("led.cycle = ");
            shell.write(self.cycling_ ? "on\r\n" : "off\r\n");
            return;
        }

        if (Shell::equals(arguments, "on")) {
            self.cycling_ = true;
            self.ticks_ = 0u;
            shell.ok();
            return;
        }

        if (Shell::equals(arguments, "off")) {
            self.cycling_ = false;
            shell.ok();
            return;
        }

        shell.error("expected 'on' or 'off'");
    }

private:
    enum class BitOperation { On, Off, Toggle };
    static constexpr std::uint32_t kTicksPerPattern = 600000u;

    static LedCommands &instance(void *context)
    {
        return *static_cast<LedCommands *>(context);
    }

    static void update_bit(void *context, Shell &shell, const char *arguments,
                           BitOperation operation)
    {
        auto &self = instance(context);
        std::uint32_t index;
        if (!Shell::parse_u32(arguments, index) || index >= 8u) {
            shell.error("expected an LED index from 0 to 7");
            return;
        }

        self.cycling_ = false;
        if (operation == BitOperation::On)
            self.led_.on(index);
        else if (operation == BitOperation::Off)
            self.led_.off(index);
        else
            self.led_.toggle(index);
        shell.ok();
    }

    BoardLed led_;
    bool cycling_ = true;
    std::size_t pattern_ = 0u;
    std::uint32_t ticks_ = 0u;
};

} // namespace

int main()
{
    BoardUart uart;
    LedCommands led_commands;
    const Shell::Command commands[] = {
        {"led.status", "led.status", "Read the current 8-bit LED output.",
         LedCommands::status, &led_commands},
        {"led.control", "led.control [=] <value>",
         "Write the LED output and pause cycling.", LedCommands::control,
         &led_commands},
        {"led.on", "led.on <index>", "Turn on one LED and pause cycling.",
         LedCommands::on, &led_commands},
        {"led.off", "led.off <index>", "Turn off one LED and pause cycling.",
         LedCommands::off, &led_commands},
        {"led.toggle", "led.toggle <index>",
         "Toggle one LED and pause cycling.", LedCommands::toggle,
         &led_commands},
        {"led.cycle", "led.cycle [on|off]", "Read or change cycling state.",
         LedCommands::cycle, &led_commands},
    };
    Shell shell(uart, commands, sizeof(commands) / sizeof(commands[0]));

    uart.begin();
    shell.begin();
    while (true) {
        shell.poll();
        led_commands.tick();
    }
}
