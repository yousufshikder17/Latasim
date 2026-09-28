#include "lpc17xx/eint.hpp"

namespace latasim::lpc17xx {

bool ExternalInterrupt::eint0_active() const { return selected_ && high_ == ((polarity_ & 1u) != 0); }

void ExternalInterrupt::sample_level() {
    if (!(mode_ & 1u) && eint0_active()) flags_ |= 1u;
}

void ExternalInterrupt::write_extint(std::uint32_t value) {
    flags_ &= ~(value & 0xFu);
    sample_level();  // "In level-sensitive mode the interrupt is cleared only when the pin is in its inactive state"
}

void ExternalInterrupt::write_extmode(std::uint32_t value) {
    mode_ = value & 0xFu;
    sample_level();
}

void ExternalInterrupt::write_extpolar(std::uint32_t value) {
    polarity_ = value & 0xFu;
    sample_level();
}

void ExternalInterrupt::eint0_input(bool selected, bool high) {
    const bool edge = selected && selected_ && high != high_;
    const bool rising = high && !high_;
    selected_ = selected;
    high_ = high;
    if (mode_ & 1u) {
        if (edge && rising == ((polarity_ & 1u) != 0)) flags_ |= 1u;
    } else {
        sample_level();
    }
}

}  // namespace latasim::lpc17xx
