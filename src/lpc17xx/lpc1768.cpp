#include "lpc17xx/lpc1768.hpp"

#include <cstdio>
#include <string>

namespace latasim::lpc17xx {
namespace {

std::string fault_message(std::uint32_t address) {
    char text[64];
    std::snprintf(text, sizeof text, "bus fault: no register at 0x%08X", static_cast<unsigned>(address));
    return text;
}

bool is_systick(std::uint32_t address) { return address - kSysTickBase < 0x10; }

// SysTick registers are word accesses here; narrow access is not modeled.
SysTickReg systick_reg(std::uint32_t address, unsigned size) {
    if (size != 4 || (address & 3u) != 0) throw BusFault(address);
    return static_cast<SysTickReg>(address - kSysTickBase);
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
        return load(word_address, 4);
    } catch (const BusFault&) {
        throw BusFault(alias);
    }
}

BusFault::BusFault(std::uint32_t address) : std::runtime_error(fault_message(address)), address_(address) {}

// Maps a GPIO address and access size (1, 2 or 4 bytes) to a register and the byte
// lanes it covers. Narrow accesses must be naturally aligned inside one register.
Lpc1768::GpioTarget Lpc1768::decode_gpio(std::uint32_t address, unsigned size) {
    if (address < kGpioBase || address % size != 0) throw BusFault(address);
    const std::uint32_t offset = address - kGpioBase;
    const unsigned port = offset / kGpioPortStride;
    if (port >= Gpio::kPortCount) throw BusFault(address);
    const std::uint32_t in_port = offset % kGpioPortStride;
    const auto reg = static_cast<GpioReg>(in_port & ~3u);
    const unsigned shift = (in_port & 3u) * 8;
    const std::uint32_t lanes = size == 4 ? 0xFFFFFFFFu : ((std::uint32_t{1} << (size * 8)) - 1) << shift;
    switch (reg) {
    case GpioReg::Dir:
    case GpioReg::Mask:
    case GpioReg::Pin:
    case GpioReg::Set:
    case GpioReg::Clr:
        return {port, reg, shift, lanes};
    }
    throw BusFault(address);  // reserved offsets 0x04-0x0F
}

void Lpc1768::advance_cycles(std::uint64_t cycles) {
    if (in_systick_handler_) throw std::logic_error("advance_cycles called from the SysTick handler");
    while (cycles > 0) {
        const std::uint64_t to_zero = on_systick_ && systick_.interrupt_enabled() ? systick_.cycles_to_zero() : 0;
        const std::uint64_t step = to_zero == 0 || to_zero > cycles ? cycles : to_zero;
        cycles_ += step;
        systick_.advance(step);
        cycles -= step;
        if (step == to_zero) {
            in_systick_handler_ = true;
            try {
                on_systick_();
            } catch (...) {
                in_systick_handler_ = false;
                throw;
            }
            in_systick_handler_ = false;
        }
    }
}

std::uint32_t Lpc1768::read(std::uint32_t address, unsigned size) const {
    const std::uint32_t value = load(address, size);
    trace_.record({.kind = TraceKind::Read, .address = address, .width = size, .value = value});
    return value;
}

void Lpc1768::write(std::uint32_t address, unsigned size, std::uint32_t value) {
    store(address, size, value);
    trace_.record({.kind = TraceKind::Write, .address = address, .width = size, .value = value});
    if (on_store_) on_store_();
}

std::uint32_t Lpc1768::load(std::uint32_t address, unsigned size) const {
    if (is_alias(address)) {
        // Bit-band aliases are word accesses only here; narrow alias access is not modeled.
        if (size != 4 || (address & 3u) != 0) throw BusFault(address);
        const AliasTarget t = alias_target(address);
        return (read_target(address, t.word_address) >> t.bit) & 1u;
    }
    if (address == kPconpAddress) {
        if (size != 4) throw BusFault(address);
        return pconp_;
    }
    if (is_systick(address)) return systick_.read(systick_reg(address, size));
    const GpioTarget t = decode_gpio(address, size);
    return (gpio_.read(t.port, t.reg) & t.lanes) >> t.shift;
}

void Lpc1768::store(std::uint32_t address, unsigned size, std::uint32_t value) {
    if (is_alias(address)) {
        if (size != 4 || (address & 3u) != 0) throw BusFault(address);
        const AliasTarget t = alias_target(address);
        const std::uint32_t word = read_target(address, t.word_address);
        const std::uint32_t mask = std::uint32_t{1} << t.bit;
        store(t.word_address, 4, (value & 1u) ? (word | mask) : (word & ~mask));
        return;
    }
    if (address == kPconpAddress) {
        if (size != 4) throw BusFault(address);
        pconp_ = value;
        return;
    }
    if (is_systick(address)) {
        const SysTickReg reg = systick_reg(address, size);
        // STCALIB is read-only (ARM DUI 0552A); UM10360 Table 438 says R/W. With the
        // sources disagreeing, a write is reported rather than guessed at.
        if (reg == SysTickReg::Calib) throw BusFault(address);
        systick_.write(reg, value);
        return;
    }
    const GpioTarget t = decode_gpio(address, size);
    gpio_.write(t.port, t.reg, (value << t.shift) & t.lanes, t.lanes);
}

}  // namespace latasim::lpc17xx
