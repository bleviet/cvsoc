#ifndef CVSOC_NIOSV_SHELL_JTAG_UART_HPP
#define CVSOC_NIOSV_SHELL_JTAG_UART_HPP

#include <cstddef>
#include <cstdint>

namespace cvsoc {

template <std::uintptr_t Base>
class JtagUart {
public:
    void begin() const
    {
        // The generated HAL registers an interrupt-driven JTAG UART driver
        // before main(). Disable its read/write interrupts so it cannot race
        // this deliberately polled transport for incoming bytes.
        reg(kControlOffset) = 0u;
    }

    bool read(char &value) const
    {
        const std::uint32_t data = reg(kDataOffset);
        if ((data & kReadValid) == 0u)
            return false;

        value = static_cast<char>(data & 0xffu);
        return true;
    }

    void write(char value) const
    {
        while ((reg(kControlOffset) >> 16) == 0u)
            ;

        reg(kDataOffset) = static_cast<std::uint8_t>(value);
    }

    void write(const char *text) const
    {
        while (*text != '\0')
            write(*text++);
    }

private:
    static constexpr std::uintptr_t kDataOffset = 0u;
    static constexpr std::uintptr_t kControlOffset = 4u;
    static constexpr std::uint32_t kReadValid = 1u << 15;

    static volatile std::uint32_t &reg(std::uintptr_t offset)
    {
        return *reinterpret_cast<volatile std::uint32_t *>(Base + offset);
    }
};

} // namespace cvsoc

#endif
