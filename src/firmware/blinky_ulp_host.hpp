#pragma once
// Running Keil's Blinky_ULp on a modelled board from C++ (see blinky_ulp.h).
//
// IRQ.c keeps its state in function statics (the tick counter behind clock_1s, the
// chase position and the ticks since the last step), which survive from one run
// to the next in a process: host execution has no C startup to re-initialise
// them. reset_blinky_statics() puts them back by running a scratch board with the
// potentiometer at 0 (the chase steps every tick) until the tick on which
// clock_1s is set also lights LED0: that tick leaves exactly the initial state.
// If the chase is out of phase with the seconds counter (an earlier run held it
// back), it is held back one tick at a time until they line up; each hold uses
// the potentiometer, as a slower chase would. The ADC driver's statics end clear.
// It binds the scratch board itself, so it must run while no board is bound.
#include "firmware/blinky_ulp.h"

#include "boards/mcb1700/board.hpp"
#include "host/binding.hpp"
#include "lpc17xx/lpc1768.hpp"

#include <cstdint>
#include <stdexcept>

namespace latasim::firmware {

inline constexpr std::uint64_t kBlinkyTick = 10 * lpc17xx::kCyclesPerMillisecond;

// Binds Blinky_ULp's interrupt handlers and its main loop's reaction to them.
inline void bind_blinky(lpc17xx::Lpc1768& mcu) {
    mcu.bind_handler(lpc17xx::kSysTickIrq, SysTick_Handler);
    mcu.bind_handler(lpc17xx::kAdcIrq, ADC_IRQHandler);
    mcu.on_thread_mode(blinky_ulp_main_loop_step);
}

inline void reset_blinky_statics() {
    mcb1700::Board scratch;
    host::FirmwareBinding bind(scratch);
    AD_last = 0;  // Blinky.c's copy (the host port's own variable)
    blinky_ulp_start();
    bind_blinky(scratch.mcu());
    auto tick = [&] {
        clock_1s = 0;
        scratch.mcu().advance_cycles(kBlinkyTick);
        return clock_1s != 0;
    };
    for (int second = 0; second < 16; ++second) {
        for (int t = 0; t < 100 && !tick(); ++t) {}
        if (scratch.led(0) == mcb1700::LedState::On) {
            clock_1s = 0;
            return;
        }
        // Hold the chase back one tick: a conversion of 0x100 makes the next tick
        // wait (AD_last >> 8 = 1), and the one after that brings AD_last back to 0.
        scratch.set_potentiometer(0x100);
        tick();
        scratch.set_potentiometer(0);
        tick();
    }
    throw std::logic_error("Blinky_ULp statics did not return to their initial state");
}

}  // namespace latasim::firmware
