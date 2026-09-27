#pragma once
// The LPC1768 as firmware sees it: 32-bit loads and stores to its memory map.
// This is the seam a future MMIO adapter (host-compiled firmware, emulator)
// plugs into. Only the GPIO block and its bit-band alias are mapped so far;
// anything else raises BusFault, as an unmapped access would on the chip.
#include <cstdint>
#include <stdexcept>

#include "lpc17xx/gpio.hpp"

namespace latasim::lpc17xx {

// From LPC17xx.h (LPC1700_DFP 2.6.0).
inline constexpr std::uint32_t kGpioBase = 0x2009C000;  // LPC_GPIO_BASE
inline constexpr std::uint32_t kGpioPortStride = 0x20;  // LPC_GPIOn_BASE = base + n * 0x20

// Cortex-M3 SRAM bit-band: each bit of 0x20000000-0x200FFFFF has a word alias.
inline constexpr std::uint32_t kBitBandBase = 0x20000000;
inline constexpr std::uint32_t kBitBandAliasBase = 0x22000000;
inline constexpr std::uint32_t kBitBandSize = 0x00100000;

constexpr std::uint32_t gpio_register_address(unsigned port, GpioReg reg) {
    return kGpioBase + port * kGpioPortStride + static_cast<std::uint32_t>(reg);
}

// alias = 0x22000000 + (address - 0x20000000) * 32 + bit * 4, where address is
// the byte holding the bit; for a 32-bit register, bit 0-31 of the word.
constexpr std::uint32_t bit_band_alias(std::uint32_t word_address, unsigned bit) {
    const std::uint32_t byte_address = word_address + bit / 8;
    return kBitBandAliasBase + (byte_address - kBitBandBase) * 32 + (bit % 8) * 4;
}

class BusFault : public std::runtime_error {
public:
    explicit BusFault(std::uint32_t address);
    std::uint32_t address() const { return address_; }

private:
    std::uint32_t address_;
};

class Lpc1768 {
public:
    std::uint32_t read32(std::uint32_t address) const;
    // Bit-band alias writes are a read-modify-write of the whole target word, as
    // on the Cortex-M3. For FIOPIN that copies input pin levels into the latch.
    void write32(std::uint32_t address, std::uint32_t value);

    Gpio& gpio() { return gpio_; }
    const Gpio& gpio() const { return gpio_; }

private:
    struct GpioTarget {
        unsigned port;
        GpioReg reg;
    };
    static GpioTarget decode_gpio(std::uint32_t address);
    std::uint32_t read_target(std::uint32_t alias, std::uint32_t word_address) const;

    Gpio gpio_;
};

}  // namespace latasim::lpc17xx
