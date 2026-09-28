#pragma once
// Scenarios: firmware on a modelled MCB1700 over virtual time, for GoogleTest.
//
//   Scenario s;                                  // new board, bound to host firmware, t = 0
//   blinky_ulp_start();
//   s.on_systick(SysTick_Handler);
//   s.run_until(10ms);
//   EXPECT_TRUE(s.led(1, LedState::On));
//   s.press(JoystickDirection::Up);              // at t = 10 ms
//   s.run_for(5ms);
//   EXPECT_TRUE(s.pin(1, 23, Level::Low));
//
// Times are std::chrono durations used only as units, converted exactly to core
// clock cycles; nothing here reads a clock. Inputs happen at the current virtual
// time, so timed inputs are run_until(t) followed by press/release.
//
// The checks return testing::AssertionResult: on failure the message has the
// expected and actual values, the virtual time and the last trace events. They
// observe without side effects: no MMIO, no trace events, COUNTFLAG left alone.
#include "boards/mcb1700/board.hpp"
#include "host/binding.hpp"
#include "lpc17xx/lpc1768.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <ratio>

namespace latasim::test {

// One core clock cycle at lpc17xx::kCoreClockHz. Milliseconds and microseconds
// convert implicitly and exactly; nanoseconds do not, since 1 ns is 0.1 cycle.
using Cycles = std::chrono::duration<std::uint64_t, std::ratio<1, lpc17xx::kCoreClockHz>>;

enum class Level { Low, High };

class Scenario {
public:
    Scenario();
    Scenario(const Scenario&) = delete;
    Scenario& operator=(const Scenario&) = delete;

    mcb1700::Board& board() { return board_; }
    lpc17xx::Lpc1768& mcu() { return board_.mcu(); }
    Cycles now() const { return Cycles{board_.mcu().cycles()}; }

    // The firmware's SysTick_Handler (see Lpc1768::on_systick).
    void on_systick(std::function<void()> handler);

    void run_for(Cycles duration);
    // Runs to an absolute virtual time; a time already passed is an error.
    void run_until(Cycles time);

    void press(mcb1700::JoystickDirection direction) { board_.press(direction); }
    void release(mcb1700::JoystickDirection direction) { board_.release(direction); }
    void press_int0() { board_.press_int0(); }
    void release_int0() { board_.release_int0(); }

    testing::AssertionResult led(unsigned index, mcb1700::LedState expected) const;
    testing::AssertionResult pin(unsigned port, unsigned pin, Level expected) const;
    testing::AssertionResult reg(std::uint32_t address, std::uint32_t expected) const;

private:
    testing::AssertionResult fail(const std::string& what) const;

    mcb1700::Board board_;
    host::FirmwareBinding bind_{board_};
};

}  // namespace latasim::test
