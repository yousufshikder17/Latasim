#pragma once
// The LPC1768 external interrupt EINT0 (UM10360 section 3.6), on P2.10.
//
// EXTINT holds the flags, EXTMODE selects level (0) or edge (1) sensitivity and
// EXTPOLAR low/falling (0) or high/rising (1); all reset to 0: level, low-active.
// EINT0 sees P2.10 only while PINSEL4 selects its EINT0 function. In level mode the
// flag is set while the pin is at the active level and cannot be cleared then; in
// edge mode it is set by the selected edge and cleared by writing 1 (tables 9-12).
// The flag is EINT0's interrupt line into the NVIC.
//
// Not modelled: EINT1-3 (their bits are stored, their pins are not watched),
// wake-up from power-down, and the spurious flags the manual warns a mode or
// polarity change can cause (3.6.3). Changing to level mode with the pin active
// sets the flag, as that level would.
#include <cstdint>

namespace latasim::lpc17xx {

inline constexpr std::uint32_t kExtintAddress = 0x400FC140;
inline constexpr std::uint32_t kExtmodeAddress = 0x400FC148;
inline constexpr std::uint32_t kExtpolarAddress = 0x400FC14C;

class ExternalInterrupt {
public:
    std::uint32_t extint() const { return flags_; }
    std::uint32_t extmode() const { return mode_; }
    std::uint32_t extpolar() const { return polarity_; }
    void write_extint(std::uint32_t value);  // write 1 to clear
    void write_extmode(std::uint32_t value);
    void write_extpolar(std::uint32_t value);

    // P2.10's level and whether PINSEL4 selects EINT0, after any change.
    void eint0_input(bool selected, bool high);
    bool eint0() const { return (flags_ & 1u) != 0; }

private:
    bool eint0_active() const;  // the level-mode active state
    void sample_level();

    std::uint32_t flags_ = 0, mode_ = 0, polarity_ = 0;
    bool selected_ = false;
    bool high_ = true;
};

}  // namespace latasim::lpc17xx
