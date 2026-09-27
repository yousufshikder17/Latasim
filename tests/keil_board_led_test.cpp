#include "boards/mcb1700/board.hpp"
#include "boards/mcb1700/keil_board_led.hpp"
#include "lpc17xx/keil_gpio_driver.hpp"

#include <gtest/gtest.h>

using latasim::lpc17xx::gpio_register_address;
using latasim::lpc17xx::GpioReg;
using latasim::lpc17xx::KeilGpioDriver;
using latasim::mcb1700::Board;
using latasim::mcb1700::KeilBoardLed;
using latasim::mcb1700::kLedCount;
using latasim::mcb1700::LedState;

TEST(KeilGpioDriver, SetDirTouchesOnlyItsBit) {
    Board board;
    KeilGpioDriver gpio(board.mcu());
    gpio.set_dir(1, 28, true);
    gpio.set_dir(1, 29, true);
    gpio.set_dir(1, 28, false);
    EXPECT_EQ(board.mcu().read32(gpio_register_address(1, GpioReg::Dir)), 1u << 29);
}

TEST(KeilGpioDriver, PinWriteAndPinReadRoundTrip) {
    Board board;
    KeilGpioDriver gpio(board.mcu());
    gpio.set_dir(2, 2, true);
    gpio.pin_write(2, 2, 1);
    EXPECT_EQ(gpio.pin_read(2, 2), 1u);
    gpio.pin_write(2, 2, 0);
    EXPECT_EQ(gpio.pin_read(2, 2), 0u);
}

TEST(KeilBoardLed, InitializeLeavesAllLedsOff) {
    Board board;
    KeilBoardLed leds(board.mcu());
    EXPECT_EQ(leds.initialize(), 0);
    for (unsigned i = 0; i < kLedCount; ++i) EXPECT_EQ(board.led(i), LedState::Off) << "LED" << i;
}

// The same end state µVision reported after Keil Blinky's LED_Initialize (E2,
// spikes/uvsim-script/e2-run1.out): LED pins driven low, everything else pulled high.
TEST(KeilBoardLed, InitializeMatchesSimulatorE2EndState) {
    Board board;
    KeilBoardLed(board.mcu()).initialize();
    EXPECT_EQ(board.mcu().read32(gpio_register_address(1, GpioReg::Pin)), 0x4FFFC713u);
    EXPECT_EQ(board.mcu().read32(gpio_register_address(2, GpioReg::Pin)), 0x00003F83u);
    EXPECT_EQ(board.mcu().read32(gpio_register_address(1, GpioReg::Dir)), 0xB0000000u);
    EXPECT_EQ(board.mcu().read32(gpio_register_address(2, GpioReg::Dir)), 0x0000007Cu);
}

TEST(KeilBoardLed, OnAndOffControlOneLed) {
    Board board;
    KeilBoardLed leds(board.mcu());
    leds.initialize();
    EXPECT_EQ(leds.on(3), 0);
    EXPECT_EQ(board.led(3), LedState::On);
    EXPECT_EQ(board.led(2), LedState::Off);
    EXPECT_EQ(leds.off(3), 0);
    EXPECT_EQ(board.led(3), LedState::Off);
}

TEST(KeilBoardLed, OutOfRangeLedReturnsMinusOneAndChangesNothing) {
    Board board;
    KeilBoardLed leds(board.mcu());
    leds.initialize();
    EXPECT_EQ(leds.on(kLedCount), -1);
    EXPECT_EQ(leds.off(kLedCount), -1);
    for (unsigned i = 0; i < kLedCount; ++i) EXPECT_EQ(board.led(i), LedState::Off);
}

TEST(KeilBoardLed, SetOutShowsABitPattern) {
    Board board;
    KeilBoardLed leds(board.mcu());
    leds.initialize();
    EXPECT_EQ(leds.set_out(0xA5), 0);  // 1010 0101
    for (unsigned i = 0; i < kLedCount; ++i)
        EXPECT_EQ(board.led(i), ((0xA5u >> i) & 1u) ? LedState::On : LedState::Off) << "LED" << i;
}

TEST(KeilBoardLed, WithoutInitializeOnLatchesButTheLedStaysUndriven) {
    Board board;
    KeilBoardLed leds(board.mcu());
    leds.on(0);
    EXPECT_EQ(board.led(0), LedState::Undriven) << "FIODIR never set";
    leds.initialize();
    EXPECT_EQ(board.led(0), LedState::Off) << "LED_Initialize writes 0 before anything shows";
}

TEST(KeilBoardLed, CountIsEight) {
    Board board;
    EXPECT_EQ(KeilBoardLed(board.mcu()).count(), 8u);
}
