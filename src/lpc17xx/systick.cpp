#include "lpc17xx/systick.hpp"

namespace latasim::lpc17xx {

std::uint32_t SysTick::read(SysTickReg reg) {
    const std::uint32_t value = peek(reg);
    if (reg == SysTickReg::Ctrl) countflag_ = false;
    return value;
}

std::uint32_t SysTick::peek(SysTickReg reg) const {
    switch (reg) {
    case SysTickReg::Ctrl: return ctrl_ | (countflag_ ? kSysTickCountflag : 0u);
    case SysTickReg::Load: return reload_;
    case SysTickReg::Val: return current_;
    case SysTickReg::Calib: return kSysTickCalib;
    }
    return 0;
}

void SysTick::write(SysTickReg reg, std::uint32_t value) {
    switch (reg) {
    case SysTickReg::Ctrl: {
        const bool starting = (value & kSysTickEnable) && !(ctrl_ & kSysTickEnable);
        ctrl_ = value & (kSysTickEnable | kSysTickTickint | kSysTickClksource);
        if (starting) current_ = reload_;  // "When ENABLE is set to 1, the counter loads the RELOAD value"
        break;
    }
    case SysTickReg::Load: reload_ = value & kSysTickCounterMask; break;
    case SysTickReg::Val:
        current_ = 0;
        countflag_ = false;
        break;
    case SysTickReg::Calib: break;  // read-only; Lpc1768 faults before getting here
    }
}

std::uint64_t SysTick::cycles_to_zero() const {
    if (!(ctrl_ & kSysTickEnable) || !(ctrl_ & kSysTickClksource)) return 0;
    if (current_ != 0) return current_;
    return reload_ == 0 ? 0 : std::uint64_t{reload_} + 1;
}

// Closed form of this per-clock rule, so advancing by millions of cycles is cheap:
//   if (current == 0) current = reload;
//   else if (--current == 0) { countflag = true; ++zeros; }
std::uint64_t SysTick::advance(std::uint64_t cycles) {
    if (!(ctrl_ & kSysTickEnable) || !(ctrl_ & kSysTickClksource) || cycles == 0) return 0;
    if (current_ == 0) {  // the first clock wraps to RELOAD
        --cycles;
        current_ = reload_;
        if (current_ == 0) return 0;  // RELOAD 0 stays at 0 and never counts to 0
    }
    if (cycles < current_) {
        current_ -= static_cast<std::uint32_t>(cycles);
        return 0;
    }
    // Count down to 0, then whole periods of wrap-to-RELOAD and count down again.
    cycles -= current_;
    current_ = 0;
    countflag_ = true;
    if (reload_ == 0) return 1;
    const std::uint64_t period = std::uint64_t{reload_} + 1;
    const std::uint64_t rest = cycles % period;
    current_ = rest == 0 ? 0 : static_cast<std::uint32_t>(reload_ - (rest - 1));
    return 1 + cycles / period;
}

}  // namespace latasim::lpc17xx
