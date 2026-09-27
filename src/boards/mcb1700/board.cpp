#include "boards/mcb1700/board.hpp"

#include <stdexcept>
#include <string>

namespace vwb::mcb1700 {

const char* to_string(LedState state) {
    switch (state) {
    case LedState::Off: return "OFF";
    case LedState::On: return "ON";
    case LedState::Undriven: return "UNDRIVEN";
    }
    return "?";
}

LedState Board::led(unsigned index) const {
    if (index >= kLedCount) throw std::out_of_range("LED " + std::to_string(index) + " out of range");
    const PinRef p = kLedPins[index];
    const lpc17xx::Gpio& gpio = mcu_.gpio();
    if (!gpio.is_output(p.port, p.pin)) return LedState::Undriven;
    return gpio.pin_level(p.port, p.pin) == kLedActiveHigh ? LedState::On : LedState::Off;
}

}  // namespace vwb::mcb1700
