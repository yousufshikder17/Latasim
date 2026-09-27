#include "lpc17xx/lpc1768.hpp"

#include <cstdio>
#include <string>

namespace vwb::lpc17xx {
namespace {

std::string fault_message(std::uint32_t address) {
    char text[64];
    std::snprintf(text, sizeof text, "bus fault: no register at 0x%08X", static_cast<unsigned>(address));
    return text;
}

bool is_alias(std::uint32_t address) {
    return address >= kBitBandAliasBase && address - kBitBandAliasBase < kBitBandSize * 32;
}

struct AliasTarget {
    std::uint32_t word_address;
    unsigned bit;
};

AliasTarget alias_target(std::uint32_t alias) {
    const std::uint32_t offset = alias - kBitBandAliasBase;
    const std::uint32_t byte_address = kBitBandBase + offset / 32;
    const unsigned bit_in_byte = (offset % 32) / 4;
    return {byte_address & ~3u, (byte_address & 3u) * 8 + bit_in_byte};
}

}  // namespace

// Reads the word an alias points at; a fault names the alias the firmware used.
std::uint32_t Lpc1768::read_target(std::uint32_t alias, std::uint32_t word_address) const {
    try {
        return read32(word_address);
    } catch (const BusFault&) {
        throw BusFault(alias);
    }
}

BusFault::BusFault(std::uint32_t address) : std::runtime_error(fault_message(address)), address_(address) {}

Lpc1768::GpioTarget Lpc1768::decode_gpio(std::uint32_t address) {
    if (address < kGpioBase || (address & 3u) != 0) throw BusFault(address);
    const std::uint32_t offset = address - kGpioBase;
    const unsigned port = offset / kGpioPortStride;
    if (port >= Gpio::kPortCount) throw BusFault(address);
    const auto reg = static_cast<GpioReg>(offset % kGpioPortStride);
    switch (reg) {
    case GpioReg::Dir:
    case GpioReg::Mask:
    case GpioReg::Pin:
    case GpioReg::Set:
    case GpioReg::Clr:
        return {port, reg};
    }
    throw BusFault(address);  // reserved offsets 0x04-0x0C
}

std::uint32_t Lpc1768::read32(std::uint32_t address) const {
    if (is_alias(address)) {
        if ((address & 3u) != 0) throw BusFault(address);
        const AliasTarget t = alias_target(address);
        return (read_target(address, t.word_address) >> t.bit) & 1u;
    }
    const GpioTarget t = decode_gpio(address);
    return gpio_.read(t.port, t.reg);
}

void Lpc1768::write32(std::uint32_t address, std::uint32_t value) {
    if (is_alias(address)) {
        if ((address & 3u) != 0) throw BusFault(address);
        const AliasTarget t = alias_target(address);
        const std::uint32_t word = read_target(address, t.word_address);
        const std::uint32_t mask = std::uint32_t{1} << t.bit;
        write32(t.word_address, (value & 1u) ? (word | mask) : (word & ~mask));
        return;
    }
    const GpioTarget t = decode_gpio(address);
    gpio_.write(t.port, t.reg, value);
}

}  // namespace vwb::lpc17xx
