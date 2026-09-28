#pragma once
// Keil's Blinky_ULp on a fresh board (see firmware/blinky_ulp.h; reset_irq_statics
// is firmware/blinky_ulp_host.hpp's reset_blinky_statics).
#include "firmware/blinky_ulp_host.hpp"

#include <memory>

namespace latasim::test {

using firmware::bind_blinky;
using firmware::kBlinkyTick;
inline void reset_irq_statics() { firmware::reset_blinky_statics(); }

struct Blinky {
    mcb1700::Board board;
    Blinky() {
        reset_irq_statics();
        bind_ = std::make_unique<host::FirmwareBinding>(board);
        blinky_ulp_start();
        bind_blinky(board.mcu());
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
