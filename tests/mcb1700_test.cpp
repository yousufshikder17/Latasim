#include "boards/mcb1700/board.hpp"

#include <gtest/gtest.h>

#include <stdexcept>

using latasim::lpc17xx::gpio_register_address;
using latasim::lpc17xx::GpioReg;
using latasim::mcb1700::Board;
using latasim::mcb1700::kLedCount;
using latasim::mcb1700::kLedPins;
using latasim::mcb1700::LedState;

namespace {

void drive(Board& board, unsigned port, unsigned pin, bool high) {
    auto& mcu = board.mcu();
    const std::uint32_t dir = gpio_register_address(port, GpioReg::Dir);
    mcu.write32(dir, mcu.read32(dir) | (1u << pin));
    mcu.write32(gpio_register_address(port, high ? GpioReg::Set : GpioReg::Clr), 1u << pin);
}

}  // namespace

TEST(Mcb1700, LedPinsMatchTheKeilBoardDriver) {  // LED_MCB1700.c, pin-map.md
    const unsigned expected[kLedCount][2] = {{1, 28}, {1, 29}, {1, 31}, {2, 2}, {2, 3}, {2, 4}, {2, 5}, {2, 6}};
    for (unsigned i = 0; i < kLedCount; ++i) {
        EXPECT_EQ(kLedPins[i].port, expected[i][0]) << "LED" << i;
        EXPECT_EQ(kLedPins[i].pin, expected[i][1]) << "LED" << i;
    }
}

TEST(Mcb1700, AllLedsAreUndrivenAtReset) {
    const Board board;
    for (unsigned i = 0; i < kLedCount; ++i) EXPECT_EQ(board.led(i), LedState::Undriven) << "LED" << i;
}

TEST(Mcb1700, LedIsOnWhenItsPinDrivesHighAndOffWhenLow) {
    Board board;
    drive(board, 1, 28, true);
    EXPECT_EQ(board.led(0), LedState::On);
    drive(board, 1, 28, false);
    EXPECT_EQ(board.led(0), LedState::Off);
}

TEST(Mcb1700, EachLedFollowsOnlyItsOwnPin) {
    for (unsigned lit = 0; lit < kLedCount; ++lit) {
        Board board;
        for (unsigned i = 0; i < kLedCount; ++i) drive(board, kLedPins[i].port, kLedPins[i].pin, i == lit);
        for (unsigned i = 0; i < kLedCount; ++i)
            EXPECT_EQ(board.led(i), i == lit ? LedState::On : LedState::Off) << "lit LED" << lit << ", LED" << i;
    }
}

TEST(Mcb1700, AnInputPinDoesNotDriveItsLedEvenWhenPulledHigh) {
    Board board;
    board.mcu().write32(gpio_register_address(1, GpioReg::Set), 1u << 28);  // latched, but still an input
    EXPECT_TRUE(board.mcu().gpio().pin_level(1, 28));
    EXPECT_EQ(board.led(0), LedState::Undriven);
}

TEST(Mcb1700, LedsIgnoreNeighbouringPins) {
    Board board;
    drive(board, 1, 28, false);
    drive(board, 1, 27, true);   // P1.27 and P1.30 are not LEDs
    drive(board, 1, 30, true);
    EXPECT_EQ(board.led(0), LedState::Off);
    EXPECT_EQ(board.led(1), LedState::Undriven);
    EXPECT_EQ(board.led(2), LedState::Undriven);
}

TEST(Mcb1700, LedIndexOutOfRangeThrows) {
    const Board board;
    EXPECT_THROW(board.led(kLedCount), std::out_of_range);
}

TEST(Mcb1700, LedStatesHaveReadableNames) {
    EXPECT_STREQ(to_string(LedState::On), "ON");
    EXPECT_STREQ(to_string(LedState::Off), "OFF");
    EXPECT_STREQ(to_string(LedState::Undriven), "UNDRIVEN");
}
