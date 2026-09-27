#pragma once
// The LPC1768 as firmware sees it: 8/16/32-bit loads and stores to its memory map.
// This is the seam a future MMIO adapter (host-compiled firmware, emulator)
// plugs into. Only the GPIO block, its bit-band alias and PCONP are mapped so far;
// anything else raises BusFault, as an unmapped access would on the chip.
#include <cstdint>
#include <stdexcept>

#include <functional>

#include "lpc17xx/gpio.hpp"
#include "trace/trace.hpp"

namespace latasim::lpc17xx {

// From LPC17xx.h (LPC1700_DFP 2.6.0).
inline constexpr std::uint32_t kGpioBase = 0x2009C000;  // LPC_GPIO_BASE
inline constexpr std::uint32_t kGpioPortStride = 0x20;  // LPC_GPIOn_BASE = base + n * 0x20

// PCONP (UM10360 Table 46). Reset value: the sum of Table 46's per-bit reset values,
// which the LPC1768 simulator also reads at reset (E9) and Keil's SystemInit writes.
// UM10360 Table 14 and the SVD give 0x03BE instead, which contradicts Table 46.
inline constexpr std::uint32_t kPconpAddress = 0x400FC0C4;
inline constexpr std::uint32_t kPconpReset = 0x042887DE;
inline constexpr std::uint32_t kPconpGpio = std::uint32_t{1} << 15;  // PCGPIO

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
    // Loads and stores of 8, 16 and 32 bits. Narrow GPIO accesses address the byte
    // and halfword registers of LPC17xx.h (FIO1PIN0, FIO1SETH, ...) and must be
    // naturally aligned. Bit-band aliases accept 32-bit accesses only.
    std::uint8_t read8(std::uint32_t address) const { return static_cast<std::uint8_t>(read(address, 1)); }
    std::uint16_t read16(std::uint32_t address) const { return static_cast<std::uint16_t>(read(address, 2)); }
    std::uint32_t read32(std::uint32_t address) const { return read(address, 4); }
    void write8(std::uint32_t address, std::uint8_t value) { write(address, 1, value); }
    void write16(std::uint32_t address, std::uint16_t value) { write(address, 2, value); }
    // Bit-band alias writes are a read-modify-write of the whole target word, as
    // on the Cortex-M3. For FIOPIN that copies input pin levels into the latch.
    void write32(std::uint32_t address, std::uint32_t value) { write(address, 4, value); }

    Gpio& gpio() { return gpio_; }
    const Gpio& gpio() const { return gpio_; }

    // Every successful load and store above is traced, with the address and width
    // the firmware used; a faulting access records nothing. A bit-band alias access
    // is one event at the alias address.
    Trace& trace() { return trace_; }
    const Trace& trace() const { return trace_; }

    // Called after every successful store, so the board can trace what it shows.
    void on_store(std::function<void()> hook) { on_store_ = std::move(hook); }

    // PCONP is storage only: 32-bit access, and no bit gates anything. GPIO keeps
    // working with PCGPIO clear, as in the simulator (E9) and UM10360 section 9.1
    // ("Power: always enabled"); docs/phase2/open-questions.md.
    std::uint32_t pconp() const { return pconp_; }

private:
    struct GpioTarget {
        unsigned port;
        GpioReg reg;
        unsigned shift;       // bit position of the accessed lane(s)
        std::uint32_t lanes;  // the bits the access covers
    };
    static GpioTarget decode_gpio(std::uint32_t address, unsigned size);
    std::uint32_t read(std::uint32_t address, unsigned size) const;  // traced
    void write(std::uint32_t address, unsigned size, std::uint32_t value);  // traced
    std::uint32_t load(std::uint32_t address, unsigned size) const;
    void store(std::uint32_t address, unsigned size, std::uint32_t value);
    std::uint32_t read_target(std::uint32_t alias, std::uint32_t word_address) const;

    Gpio gpio_;
    std::uint32_t pconp_ = kPconpReset;
    mutable Trace trace_;  // observation only: recording a load changes no model state
    std::function<void()> on_store_;
};

}  // namespace latasim::lpc17xx
