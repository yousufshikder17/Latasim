#pragma once
// Everything observable about GPIO: all readable registers of every port, plus
// what the board shows on its LEDs. Two paths converge when their snapshots match.
#include <array>
#include <cstdint>

#include "boards/mcb1700/board.hpp"

namespace latasim::test {

struct GpioSnapshot {
    std::array<std::array<std::uint32_t, 4>, lpc17xx::Gpio::kPortCount> regs{};  // DIR, MASK, PIN, SET
    std::array<mcb1700::LedState, mcb1700::kLedCount> leds{};
    bool operator==(const GpioSnapshot&) const = default;
};

inline GpioSnapshot snapshot(const mcb1700::Board& board) {
    using lpc17xx::GpioReg;
    GpioSnapshot s;
    const lpc17xx::Gpio& g = board.mcu().gpio();
    for (unsigned p = 0; p < lpc17xx::Gpio::kPortCount; ++p)
        s.regs[p] = {g.read(p, GpioReg::Dir), g.read(p, GpioReg::Mask), g.read(p, GpioReg::Pin),
                     g.read(p, GpioReg::Set)};
    for (unsigned i = 0; i < mcb1700::kLedCount; ++i) s.leds[i] = board.led(i);
    return s;
}

}  // namespace latasim::test
