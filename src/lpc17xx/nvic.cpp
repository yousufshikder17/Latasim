#include "lpc17xx/nvic.hpp"

#include <cstdio>
#include <stdexcept>
#include <string>

#include "lpc17xx/lpc1768.hpp"

namespace latasim::lpc17xx {
namespace {

// ARM DUI 0552A table 4-2 (NVIC) and 4-12 (SCB).
constexpr std::uint32_t kIser = 0xE000E100;
constexpr std::uint32_t kIcer = 0xE000E180;
constexpr std::uint32_t kIspr = 0xE000E200;
constexpr std::uint32_t kIcpr = 0xE000E280;
constexpr std::uint32_t kIabr = 0xE000E300;
constexpr std::uint32_t kIpr = 0xE000E400;
constexpr std::uint32_t kIprEnd = kIpr + 36;  // IPR0-8; the byte past IRQ34 is RAZ/WI
constexpr std::uint32_t kShpr3 = 0xE000ED20;
constexpr std::uint32_t kShpr3SysTick = kShpr3 + 3;          // PRI_15

// One of the 32-bit bitmap registers: which bank (0 or 1) at which base, or -1.
int bank(std::uint32_t address, std::uint32_t base) {
    if (address == base) return 0;
    if (address == base + 4) return 1;
    return -1;
}

std::uint32_t word_bits(const std::bitset<kExternalIrqCount>& bits, int word) {
    std::uint32_t value = 0;
    for (int b = 0; b < 32; ++b) {
        const int irq = word * 32 + b;
        if (irq < kExternalIrqCount && bits[static_cast<std::size_t>(irq)]) value |= 1u << b;
    }
    return value;
}

}  // namespace

std::string irq_name(int irq) {
    switch (irq) {
    case kSysTickIrq: return "SysTick";
    case kTimer0Irq: return "TIMER0";
    case kTimer1Irq: return "TIMER1";
    case kTimer2Irq: return "TIMER2";
    case kTimer3Irq: return "TIMER3";
    case kEint0Irq: return "EINT0";
    case kAdcIrq: return "ADC";
    case 24: return "USB";
    }
    return "IRQ" + std::to_string(irq);
}

std::size_t Nvic::index(int irq) {
    if (irq < 0 || irq >= kExternalIrqCount) throw std::out_of_range("IRQ " + std::to_string(irq) + " out of range");
    return static_cast<std::size_t>(irq);
}

bool Nvic::maps(std::uint32_t address) {
    return (address >= kIser && address < kIprEnd) || (address >= kShpr3 && address < kShpr3 + 4);
}

namespace {

// Byte-addressable priority registers: IPR0-8 and SHPR3. Accesses stay inside one
// of the two and are naturally aligned.
bool is_priority(std::uint32_t address, unsigned size) {
    const bool ipr = address >= kIpr && address < kIprEnd;
    const bool shpr = address >= kShpr3 && address < kShpr3 + 4;
    if (!ipr && !shpr) return false;
    if (address % size != 0) throw BusFault(address);
    return true;
}

}  // namespace

std::uint32_t Nvic::read(std::uint32_t address, unsigned size) const {
    if (is_priority(address, size)) {
        std::uint32_t value = 0;
        for (unsigned i = 0; i < size; ++i) {
            const std::uint32_t a = address + i;
            std::uint8_t byte = 0;  // IPR8's last byte, SVCall/PendSV bytes: read 0
            if (a >= kIpr && a < kIpr + kExternalIrqCount) byte = priority_[a - kIpr];
            else if (a == kShpr3SysTick) byte = systick_priority_;
            value |= std::uint32_t{byte} << (8 * i);
        }
        return value;
    }
    if (size != 4) throw BusFault(address);
    int word;
    if ((word = bank(address, kIser)) >= 0 || (word = bank(address, kIcer)) >= 0) return word_bits(enabled_, word);
    if ((word = bank(address, kIspr)) >= 0 || (word = bank(address, kIcpr)) >= 0) return word_bits(pending_, word);
    if ((word = bank(address, kIabr)) >= 0) return word_bits(active_, word);
    throw BusFault(address);
}

void Nvic::write(std::uint32_t address, unsigned size, std::uint32_t value) {
    if (is_priority(address, size)) {
        for (unsigned i = 0; i < size; ++i) {
            const std::uint32_t a = address + i;
            // Only the top kPriorityBits are implemented; the rest read 0.
            const auto byte = static_cast<std::uint8_t>((value >> (8 * i)) & kPriorityMask);
            if (a >= kIpr && a < kIpr + kExternalIrqCount) priority_[a - kIpr] = byte;
            else if (a == kShpr3SysTick) systick_priority_ = byte;
            // IPR8's last byte and SHPR3's SVCall/PendSV bytes: ignored, read 0
        }
        return;
    }
    if (size != 4) throw BusFault(address);
    auto apply = [&](int word, auto op) {
        for (int b = 0; b < 32; ++b) {
            const int irq = word * 32 + b;
            if (irq < kExternalIrqCount && (value >> b) & 1u) op(static_cast<std::size_t>(irq));
        }
    };
    int word;
    if ((word = bank(address, kIser)) >= 0) return apply(word, [&](std::size_t i) { enabled_[i] = true; });
    if ((word = bank(address, kIcer)) >= 0) return apply(word, [&](std::size_t i) { enabled_[i] = false; });
    if ((word = bank(address, kIspr)) >= 0) return apply(word, [&](std::size_t i) { pending_[i] = true; });
    if ((word = bank(address, kIcpr)) >= 0)  // an asserted line pends again at once (ARM 4.2.9)
        return apply(word, [&](std::size_t i) { pending_[i] = false; sample(i); });
    throw BusFault(address);  // IABR is read-only; everything else is unmapped
}

void Nvic::set_line(int irq, bool asserted) {
    const std::size_t i = index(irq);
    line_[i] = asserted;
    sample(i);
}

void Nvic::enter(int irq) {
    if (irq == kSysTickIrq) {
        systick_pending_ = false;
        systick_active_ = true;
        return;
    }
    const std::size_t i = index(irq);
    pending_[i] = false;
    active_[i] = true;
}

void Nvic::exit(int irq) {
    if (irq == kSysTickIrq) {
        systick_active_ = false;
        return;
    }
    const std::size_t i = index(irq);
    active_[i] = false;
    sample(i);
}

}  // namespace latasim::lpc17xx
