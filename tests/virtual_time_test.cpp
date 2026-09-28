// Virtual time: integer core clock cycles, advanced only explicitly.
#include "boards/mcb1700/board.hpp"
#include "boards/mcb1700/keil_board_led.hpp"
#include "lpc17xx/lpc1768.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

using latasim::lpc17xx::kCoreClockHz;
using latasim::lpc17xx::kCyclesPerMillisecond;
using latasim::lpc17xx::Lpc1768;
using latasim::mcb1700::Board;

TEST(VirtualTime, StartsAtZero) {
    Lpc1768 mcu;
    EXPECT_EQ(mcu.cycles(), 0u);
    Board board;
    EXPECT_EQ(board.mcu().cycles(), 0u);
}

TEST(VirtualTime, AdvancesByExactlyTheRequestedCycles) {
    Lpc1768 mcu;
    mcu.advance_cycles(1);
    EXPECT_EQ(mcu.cycles(), 1u);
    mcu.advance_cycles(0);
    EXPECT_EQ(mcu.cycles(), 1u);
    mcu.advance_cycles(999'999);
    EXPECT_EQ(mcu.cycles(), 1'000'000u);
    const std::uint64_t large = std::numeric_limits<std::uint32_t>::max() + std::uint64_t{7};
    mcu.advance_cycles(large);
    EXPECT_EQ(mcu.cycles(), 1'000'000u + large) << "64-bit: beyond 42.9 s at 100 MHz";
}

// Host work takes no virtual time: only advance_cycles moves the clock.
TEST(VirtualTime, FirmwareActivityAloneDoesNotAdvanceTime) {
    Board board;
    latasim::mcb1700::KeilBoardLed leds(board.mcu());
    for (int i = 0; i < 1000; ++i) {
        leds.initialize();
        leds.set_out(static_cast<std::uint32_t>(i));
        board.press_int0();
        board.release_int0();
    }
    EXPECT_EQ(board.mcu().cycles(), 0u);
}

TEST(VirtualTime, CoreClockIsTheMcb1700SystemInitFrequency) {
    EXPECT_EQ(kCoreClockHz, 100'000'000u);
    EXPECT_EQ(kCyclesPerMillisecond, 100'000u);
}

TEST(VirtualTime, SeparateMachinesKeepSeparateTime) {
    Board a, b;
    a.mcu().advance_cycles(5);
    EXPECT_EQ(a.mcu().cycles(), 5u);
    EXPECT_EQ(b.mcu().cycles(), 0u);
}
