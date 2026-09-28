#pragma once
// Keil's Blinky_ULp on a fresh board (see firmware/blinky_ulp.h).
//
// IRQ.c keeps its state in function statics (the tick counter behind clock_1s and
// the chase position), which survive from one run to the next in a process: host
// execution has no C startup to re-initialise them. reset_irq_statics() puts them
// back to their initial values by running a scratch board until the tick on which
// clock_1s is set also lights LED0. That tick leaves exactly the initial state
// (tick counter 0, chase at LED0); it recurs every 200 ticks.
#include "firmware/blinky_ulp.h"

#include "boards/mcb1700/board.hpp"
#include "host/binding.hpp"
#include "lpc17xx/lpc1768.hpp"

#include <cstdint>
#include <memory>
#include <stdexcept>

namespace latasim::test {

inline constexpr std::uint64_t kBlinkyTick = 10 * lpc17xx::kCyclesPerMillisecond;

inline void reset_irq_statics() {
    mcb1700::Board scratch;
    host::FirmwareBinding bind(scratch);
    blinky_ulp_start();
    scratch.mcu().bind_handler(lpc17xx::kSysTickIrq, SysTick_Handler);
    for (int tick = 0; tick < 200; ++tick) {
        clock_1s = 0;
        scratch.mcu().advance_cycles(kBlinkyTick);
        if (clock_1s && scratch.led(0) == mcb1700::LedState::On) {
            clock_1s = 0;
            return;
        }
    }
    throw std::logic_error("Blinky_ULp statics did not return to their initial state within 200 ticks");
}

struct Blinky {
    mcb1700::Board board;
    Blinky() {
        reset_irq_statics();
        bind_ = std::make_unique<host::FirmwareBinding>(board);
        blinky_ulp_start();
        board.mcu().bind_handler(lpc17xx::kSysTickIrq, SysTick_Handler);
    }
    void run(std::uint64_t cycles) { board.mcu().advance_cycles(cycles); }
    // The one LED on, or -1 if none or several.
    int lit() const {
        int on = -1;
        for (unsigned i = 0; i < mcb1700::kLedCount; ++i) {
            if (board.led(i) != mcb1700::LedState::On) continue;
            if (on != -1) return -1;
            on = static_cast<int>(i);
        }
        return on;
    }

private:
    std::unique_ptr<host::FirmwareBinding> bind_;  // bound after the scratch run unbinds
};

}  // namespace latasim::test
