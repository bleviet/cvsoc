#ifndef CVSOC_NIOSV_LED_HPP
#define CVSOC_NIOSV_LED_HPP

#include <cstdint>

template <std::uintptr_t Base, unsigned Width>
class Led {
public:
    static_assert(Width > 0u && Width <= 32u, "LED width must be 1 to 32 bits");

    std::uint32_t status() const
    {
        return data_register() & mask();
    }

    void control(std::uint32_t value)
    {
        data_register() = value & mask();
    }

    bool on(unsigned index)
    {
        return update_bit(index, true);
    }

    bool off(unsigned index)
    {
        return update_bit(index, false);
    }

    bool toggle(unsigned index)
    {
        if (index >= Width)
            return false;

        control(status() ^ (1u << index));
        return true;
    }

private:
    static constexpr std::uint32_t mask()
    {
        return 0xffffffffu >> (32u - Width);
    }

    static volatile std::uint32_t &data_register()
    {
        return *reinterpret_cast<volatile std::uint32_t *>(Base);
    }

    bool update_bit(unsigned index, bool set)
    {
        if (index >= Width)
            return false;

        const std::uint32_t bit = 1u << index;
        control(set ? status() | bit : status() & ~bit);
        return true;
    }
};

#endif
