#ifndef CVSOC_NIOSV_SHELL_COMMAND_SHELL_HPP
#define CVSOC_NIOSV_SHELL_COMMAND_SHELL_HPP

#include <cstddef>
#include <cstdint>

namespace cvsoc {

template <typename Uart, std::size_t LineCapacity = 80u>
class CommandShell {
public:
    using Shell = CommandShell<Uart, LineCapacity>;
    using Handler = void (*)(void *context, Shell &shell, const char *arguments);

    struct Command {
        const char *name;
        const char *usage;
        const char *description;
        Handler handler;
        void *context;
    };

    CommandShell(Uart &uart, const Command *commands, std::size_t command_count)
        : uart_(uart), commands_(commands), command_count_(command_count)
    {
    }

    void begin()
    {
        write("CVSoC register shell\r\n");
        write("Type 'help' for commands.\r\n");
        prompt();
    }

    void poll()
    {
        char value;
        while (uart_.read(value))
            accept(value);
    }

    void write(const char *text)
    {
        uart_.write(text);
    }

    void write_hex(std::uint32_t value, unsigned digits = 8u)
    {
        static constexpr char kHex[] = "0123456789abcdef";

        if (digits == 0u || digits > 8u)
            digits = 8u;

        write("0x");
        for (unsigned shift = (digits - 1u) * 4u;; shift -= 4u) {
            uart_.write(kHex[(value >> shift) & 0xfu]);
            if (shift == 0u)
                break;
        }
    }

    void ok()
    {
        write("OK\r\n");
    }

    void error(const char *message)
    {
        write("error: ");
        write(message);
        write("\r\n");
    }

    static bool no_arguments(const char *arguments)
    {
        return *skip_spaces(arguments) == '\0';
    }

    static bool parse_u32(const char *text, std::uint32_t &value)
    {
        text = skip_spaces(text);
        unsigned base = 10u;

        if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
            base = 16u;
            text += 2;
        }

        bool found_digit = false;
        std::uint32_t result = 0u;
        while (*text != '\0' && *text != ' ' && *text != '\t') {
            unsigned digit;
            if (*text >= '0' && *text <= '9')
                digit = static_cast<unsigned>(*text - '0');
            else if (*text >= 'a' && *text <= 'f')
                digit = static_cast<unsigned>(*text - 'a') + 10u;
            else if (*text >= 'A' && *text <= 'F')
                digit = static_cast<unsigned>(*text - 'A') + 10u;
            else
                return false;

            if (digit >= base)
                return false;

            found_digit = true;
            result = result * base + digit;
            ++text;
        }

        if (!found_digit || *skip_spaces(text) != '\0')
            return false;

        value = result;
        return true;
    }

    static bool equals(const char *left, const char *right)
    {
        while (*left != '\0' && *left == *right) {
            ++left;
            ++right;
        }
        return *left == *right;
    }

private:
    static const char *skip_spaces(const char *text)
    {
        while (*text == ' ' || *text == '\t')
            ++text;
        return text;
    }

    static bool starts_with(const char *text, const char *prefix)
    {
        while (*prefix != '\0') {
            if (*text++ != *prefix++)
                return false;
        }
        return true;
    }

    void accept(char value)
    {
        if (value == '\r' || value == '\n') {
            if (value == '\n' && ignore_lf_) {
                ignore_lf_ = false;
                return;
            }
            ignore_lf_ = value == '\r';
            uart_.write("\r\n");
            execute();
            prompt();
            return;
        }

        ignore_lf_ = false;
        if (value == '\b' || value == 0x7f) {
            if (length_ != 0u) {
                --length_;
                uart_.write("\b \b");
            }
            return;
        }

        if (value == 0x03) {
            length_ = 0u;
            uart_.write("^C\r\n");
            prompt();
            return;
        }

        if (value < ' ' || value > '~')
            return;

        if (length_ + 1u >= LineCapacity) {
            uart_.write("\a");
            return;
        }

        line_[length_++] = value;
        uart_.write(value);
    }

    void execute()
    {
        while (length_ != 0u &&
               (line_[length_ - 1u] == ' ' || line_[length_ - 1u] == '\t'))
            --length_;
        line_[length_] = '\0';
        length_ = 0u;

        char *name = line_;
        while (*name == ' ' || *name == '\t')
            ++name;
        if (*name == '\0')
            return;

        char *arguments = name;
        while (*arguments != '\0' && *arguments != ' ' &&
               *arguments != '\t' && *arguments != '=')
            ++arguments;

        if (*arguments != '\0') {
            *arguments++ = '\0';
            arguments = const_cast<char *>(skip_spaces(arguments));
            if (*arguments == '=')
                arguments = const_cast<char *>(skip_spaces(arguments + 1));
        }

        if (equals(name, "help")) {
            show_help(arguments);
            return;
        }

        for (std::size_t i = 0u; i < command_count_; ++i) {
            if (equals(name, commands_[i].name)) {
                commands_[i].handler(commands_[i].context, *this, arguments);
                return;
            }
        }

        error("unknown command; type 'help'");
    }

    void show_help(const char *filter)
    {
        filter = skip_spaces(filter);
        const bool filtered = *filter != '\0';
        bool found = false;

        write("help [prefix]                 Show commands\r\n");
        for (std::size_t i = 0u; i < command_count_; ++i) {
            if (filtered && !starts_with(commands_[i].name, filter))
                continue;

            found = true;
            write(commands_[i].usage);
            write("\r\n    ");
            write(commands_[i].description);
            write("\r\n");
        }

        if (filtered && !found)
            error("no commands match that prefix");
    }

    void prompt()
    {
        write("niosv> ");
    }

    Uart &uart_;
    const Command *commands_;
    std::size_t command_count_;
    char line_[LineCapacity]{};
    std::size_t length_ = 0u;
    bool ignore_lf_ = false;
};

} // namespace cvsoc

#endif
