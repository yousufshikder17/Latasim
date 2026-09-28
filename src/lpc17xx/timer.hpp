#pragma once
// LPC1768 Timer 0-3 (UM10360 chapter 21): one model, four instances.
//
// Registers: IR, TCR, TC, PR, PC, MCR, MR0-MR3 (32-bit access). The prescale
// counter PC counts PCLK edges; when it equals PR the next edge clears it and
// increments TC (21.6.6). When TC becomes equal to MRn and MCR enables an action
// for channel n: interrupt (IR bit n) and stop (TCR[0] cleared, TC and PC held)
// take effect on the next PCLK edge, and reset makes that cycle's end reset TC
// to 0 instead of incrementing it (21.7, figures 114 and 115). IR bits are
// cleared by writing 1 (21.6.1); the timer's interrupt line is any IR bit set.
// TCR[1] holds TC and PC at 0 (applied at the write rather than the next edge).
//
// Not modelled: capture (CCR, CR0, CR1), external match outputs (EMR), counter
// mode (CTCR), DMA requests; accessing them faults. A match is detected when TC
// changes to MRn, not when firmware writes MRn or TC equal to each other.
//
// Time is counted in PCLK edges; Lpc1768 converts core cycles with the divider
// from PCLKSEL, edges falling on multiples of the divider since reset.
#include <array>
#include <cstdint>

namespace latasim::lpc17xx {

inline constexpr std::array<std::uint32_t, 4> kTimerBase = {0x40004000, 0x40008000, 0x40090000, 0x40094000};
inline constexpr std::uint32_t kTimerWindow = 0x74;  // IR .. CTCR

enum class TimerReg : std::uint32_t {
    Ir = 0x00, Tcr = 0x04, Tc = 0x08, Pr = 0x0C, Pc = 0x10, Mcr = 0x14,
    Mr0 = 0x18, Mr1 = 0x1C, Mr2 = 0x20, Mr3 = 0x24,
};

class Timer {
public:
    // False for offsets that are not modelled (CCR, CR0-1, EMR, CTCR, reserved).
    static bool modelled(std::uint32_t offset);
    std::uint32_t read(TimerReg reg) const;
    void write(TimerReg reg, std::uint32_t value);

    // Advances by `edges` PCLK edges.
    void advance(std::uint64_t edges);
    // PCLK edges until the next edge that changes interrupt or run state (a match
    // with an MCR action, or the edge after one), or 0 if none is coming.
    std::uint64_t edges_to_event() const;

    bool interrupt() const { return (ir_ & 0x3F) != 0; }
    // IR bits set since the last call (for tracing match events).
    std::uint32_t take_new_flags();

private:
    bool counting() const { return (tcr_ & 1u) && !(tcr_ & 2u); }
    std::uint64_t edges_to_change() const;
    std::uint64_t edges_to_match() const;  // 0 = no match ahead
    void apply_post_match();               // the edge after a match
    void count(std::uint64_t edges);       // counts, stopping at (not past) a match
    void matched(std::uint32_t value);

    std::uint32_t ir_ = 0, tcr_ = 0, tc_ = 0, pr_ = 0, pc_ = 0, mcr_ = 0;
    std::array<std::uint32_t, 4> mr_{};
    bool post_ = false;              // actions due on the next edge
    std::uint32_t post_flags_ = 0;   // IR bits to set then
    bool post_stop_ = false;
    bool reset_next_ = false;        // the next TC change resets it to 0
    std::uint32_t new_flags_ = 0;
};

}  // namespace latasim::lpc17xx
