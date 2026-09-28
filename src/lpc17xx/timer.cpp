#include "lpc17xx/timer.hpp"

namespace latasim::lpc17xx {
namespace {

// MCR bits for channel n: interrupt 3n, reset 3n+1, stop 3n+2.
bool action(std::uint32_t mcr, unsigned n) { return ((mcr >> (3 * n)) & 7u) != 0; }
bool interrupt_on(std::uint32_t mcr, unsigned n) { return (mcr >> (3 * n)) & 1u; }
bool reset_on(std::uint32_t mcr, unsigned n) { return (mcr >> (3 * n + 1)) & 1u; }
bool stop_on(std::uint32_t mcr, unsigned n) { return (mcr >> (3 * n + 2)) & 1u; }

}  // namespace

bool Timer::modelled(std::uint32_t offset) { return offset % 4 == 0 && offset <= static_cast<std::uint32_t>(TimerReg::Mr3); }

std::uint32_t Timer::read(TimerReg reg) const {
    switch (reg) {
    case TimerReg::Ir: return ir_;
    case TimerReg::Tcr: return tcr_;
    case TimerReg::Tc: return tc_;
    case TimerReg::Pr: return pr_;
    case TimerReg::Pc: return pc_;
    case TimerReg::Mcr: return mcr_;
    case TimerReg::Mr0: case TimerReg::Mr1: case TimerReg::Mr2: case TimerReg::Mr3:
        return mr_[(static_cast<std::uint32_t>(reg) - static_cast<std::uint32_t>(TimerReg::Mr0)) / 4];
    }
    return 0;
}

void Timer::write(TimerReg reg, std::uint32_t value) {
    switch (reg) {
    case TimerReg::Ir: ir_ &= ~(value & 0x3Fu); break;  // write 1 to clear
    case TimerReg::Tcr:
        tcr_ = value & 3u;
        if (tcr_ & 2u) {  // held in reset
            tc_ = 0;
            pc_ = 0;
            reset_next_ = false;
        }
        break;
    case TimerReg::Tc: tc_ = value; break;
    case TimerReg::Pr: pr_ = value; break;
    case TimerReg::Pc: pc_ = value; break;
    case TimerReg::Mcr: mcr_ = value & 0xFFFu; break;
    case TimerReg::Mr0: case TimerReg::Mr1: case TimerReg::Mr2: case TimerReg::Mr3:
        mr_[(static_cast<std::uint32_t>(reg) - static_cast<std::uint32_t>(TimerReg::Mr0)) / 4] = value;
        break;
    }
}

std::uint32_t Timer::take_new_flags() {
    const std::uint32_t flags = new_flags_;
    new_flags_ = 0;
    return flags;
}

// Edges until PC reaches PR and the following edge changes TC. PC above PR counts
// on through 2^32 before it can equal PR (21.6.6).
std::uint64_t Timer::edges_to_change() const {
    return static_cast<std::uint64_t>(static_cast<std::uint32_t>(pr_ - pc_)) + 1;
}

std::uint64_t Timer::edges_to_match() const {
    const std::uint32_t next = reset_next_ ? 0u : tc_ + 1u;
    std::uint64_t best = 0;
    bool any = false;
    for (unsigned n = 0; n < 4; ++n) {
        if (!action(mcr_, n)) continue;
        const std::uint64_t d = static_cast<std::uint32_t>(mr_[n] - next);  // TC changes after the first
        if (!any || d < best) best = d;
        any = true;
    }
    if (!any) return 0;
    return edges_to_change() + best * (std::uint64_t{pr_} + 1);
}

std::uint64_t Timer::edges_to_event() const {
    if (post_) return 1;
    if (!counting()) return 0;
    return edges_to_match();
}

void Timer::apply_post_match() {
    post_ = false;
    ir_ |= post_flags_;
    new_flags_ |= post_flags_;
    if (post_stop_) tcr_ &= ~1u;
}

void Timer::matched(std::uint32_t value) {
    post_flags_ = 0;
    post_stop_ = false;
    for (unsigned n = 0; n < 4; ++n) {
        if (mr_[n] != value || !action(mcr_, n)) continue;
        post_ = true;
        if (interrupt_on(mcr_, n)) post_flags_ |= 1u << n;
        if (stop_on(mcr_, n)) post_stop_ = true;
        if (reset_on(mcr_, n)) reset_next_ = true;
    }
}

// Counts `edges` edges that end no later than the next match.
void Timer::count(std::uint64_t edges) {
    const std::uint64_t to_change = edges_to_change();
    if (edges < to_change) {
        pc_ += static_cast<std::uint32_t>(edges);
        return;
    }
    const std::uint64_t period = std::uint64_t{pr_} + 1;
    const std::uint64_t changes = 1 + (edges - to_change) / period;
    pc_ = static_cast<std::uint32_t>((edges - to_change) % period);
    tc_ = (reset_next_ ? 0u : tc_ + 1u) + static_cast<std::uint32_t>(changes - 1);
    reset_next_ = false;
}

void Timer::advance(std::uint64_t edges) {
    while (edges > 0) {
        if (post_) {  // the edge after a match: its actions first, then (unless stopped) a count
            apply_post_match();
            if (counting()) {
                const std::uint64_t to_match = edges_to_match();
                count(1);
                if (to_match == 1) matched(tc_);
            }
            --edges;
            continue;
        }
        if (!counting()) return;
        const std::uint64_t to_match = edges_to_match();
        const std::uint64_t step = to_match != 0 && to_match < edges ? to_match : edges;
        count(step);
        if (step == to_match) matched(tc_);
        edges -= step;
    }
}

}  // namespace latasim::lpc17xx
