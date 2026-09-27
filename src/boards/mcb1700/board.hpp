#pragma once
// Keil MCB1700 board model: the LPC1768 plus what is wired to its pins.
// LEDs only for now. Pin map: docs/phase0/mcb1700-pin-map.md.
#include <array>
#include <cstdint>

#include "lpc17xx/lpc1768.hpp"

namespace vwb::mcb1700 {

struct PinRef {
    unsigned port;
    unsigned pin;
};

inline constexpr unsigned kLedCount = 8;

// LED0-LED7 in the numbering of Keil's board driver (LED_MCB1700.c), which the
// legacy LED.c driver uses too. The board's silkscreen names them by pin.
inline constexpr std::array<PinRef, kLedCount> kLedPins{{
    {1, 28}, {1, 29}, {1, 31}, {2, 2}, {2, 3}, {2, 4}, {2, 5}, {2, 6},
}};

// LED polarity, the one place it is decided. Active-high: both Keil's board
// driver and the legacy LED.c turn an LED on by driving its pin high. Not yet
// confirmed on real hardware; one firmware source assumes active-low
// (docs/phase0/findings.md, LED polarity).
inline constexpr bool kLedActiveHigh = true;

enum class LedState {
    Off,
    On,
    Undriven,  // the pin is not an output, so the model doesn't guess
};

const char* to_string(LedState state);

class Board {
public:
    lpc17xx::Lpc1768& mcu() { return mcu_; }
    const lpc17xx::Lpc1768& mcu() const { return mcu_; }

    LedState led(unsigned index) const;

private:
    lpc17xx::Lpc1768 mcu_;
};

}  // namespace vwb::mcb1700
