// The scenario layer itself (scenario.hpp), with C++ stand-ins for firmware.
#include "scenario.hpp"

#include "boards/mcb1700/keil_board_led.hpp"
#include "lpc17xx/systick.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <stdexcept>
#include <string>

using namespace std::chrono_literals;
using latasim::lpc17xx::kSysTickClksource;
using latasim::lpc17xx::kSysTickCountflag;
using latasim::lpc17xx::kSysTickEnable;
using latasim::lpc17xx::kSysTickTickint;
using latasim::mcb1700::JoystickDirection;
using latasim::mcb1700::KeilBoardLed;
using latasim::mcb1700::LedState;
using latasim::test::Cycles;
using latasim::test::Level;
using latasim::test::Scenario;

namespace {

constexpr std::uint32_t FIO1DIR = 0x2009C020;
constexpr std::uint32_t STCTRL = 0xE000E010;

// A 1 ms SysTick whose handler toggles LED0: timed "firmware" in a few lines.
void start_blinker(Scenario& s, KeilBoardLed& leds) {
    leds.initialize();
    s.on_systick([&s, &leds] { s.board().led(0) == LedState::On ? leds.off(0) : leds.on(0); });
    s.mcu().write32(0xE000E014, 100'000 - 1);  // STRELOAD: 1 ms
    s.mcu().write32(0xE000E018, 0);            // STCURR
    s.mcu().write32(STCTRL, kSysTickClksource | kSysTickTickint | kSysTickEnable);
}

}  // namespace

TEST(Scenario, StartsFromAFreshBoardAtTimeZero) {
    Scenario s;
    EXPECT_EQ(s.now(), Cycles{0});
    EXPECT_TRUE(s.mcu().trace().events().empty());
    EXPECT_TRUE(s.led(0, LedState::Undriven));
    EXPECT_TRUE(s.pin(1, 23, Level::High)) << "joystick released";
}

TEST(Scenario, RunForAdvancesExactly) {
    Scenario s;
    s.run_for(10ms);
    EXPECT_EQ(s.now(), Cycles{1'000'000});
    s.run_for(Cycles{1});
    EXPECT_EQ(s.mcu().cycles(), 1'000'001u);
    s.run_for(3us);
    EXPECT_EQ(s.now(), 10ms + Cycles{1} + 3us);
}

TEST(Scenario, RunUntilIsAbsoluteAndRefusesThePast) {
    Scenario s;
    s.run_until(5ms);
    EXPECT_EQ(s.now(), 5ms);
    s.run_until(5ms);  // no-op
    EXPECT_EQ(s.now(), 5ms);
    EXPECT_THROW(s.run_until(4ms), std::logic_error);
    EXPECT_EQ(s.now(), 5ms);
}

TEST(Scenario, InputsHappenAtTheCurrentVirtualTime) {
    Scenario s;
    s.run_until(100ms);
    s.press(JoystickDirection::Up);
    EXPECT_TRUE(s.pin(1, 23, Level::Low));
    s.run_until(150ms);
    s.release(JoystickDirection::Up);
    EXPECT_TRUE(s.pin(1, 23, Level::High));
    s.press_int0();
    EXPECT_TRUE(s.pin(2, 10, Level::Low));
    s.release_int0();
    EXPECT_TRUE(s.pin(2, 10, Level::High));
    EXPECT_EQ(s.mcu().trace().events().size(), 4u);
}

TEST(Scenario, TimedFirmwareDrivesLedsOverVirtualTime) {
    Scenario s;
    KeilBoardLed leds(s.mcu());
    start_blinker(s, leds);
    EXPECT_TRUE(s.led(0, LedState::Off));
    s.run_until(1ms);  // first count to 0 at 99,999 cycles
    EXPECT_TRUE(s.led(0, LedState::On));
    s.run_until(2ms);
    EXPECT_TRUE(s.led(0, LedState::Off));
    s.run_for(1ms);
    EXPECT_TRUE(s.led(0, LedState::On));
}

TEST(Scenario, RegisterChecksHaveNoSideEffects) {
    Scenario s;
    KeilBoardLed leds(s.mcu());
    start_blinker(s, leds);
    s.run_until(1ms);
    const auto events = s.mcu().trace().events().size();
    EXPECT_TRUE(s.reg(STCTRL, kSysTickCountflag | kSysTickClksource | kSysTickTickint | kSysTickEnable));
    EXPECT_TRUE(s.reg(STCTRL, kSysTickCountflag | kSysTickClksource | kSysTickTickint | kSysTickEnable))
        << "COUNTFLAG still set: the check did not read STCTRL through MMIO";
    EXPECT_TRUE(s.reg(FIO1DIR, 0xB0000000));
    EXPECT_EQ(s.mcu().trace().events().size(), events) << "checks are not traced";
}

TEST(Scenario, FailedChecksExplainThemselvesAndChangeNothing) {
    Scenario s;
    KeilBoardLed leds(s.mcu());
    leds.initialize();
    leds.on(0);
    s.run_for(Cycles{250});
    const auto events = s.mcu().trace().events().size();

    const testing::AssertionResult led = s.led(0, LedState::Off);
    ASSERT_FALSE(led);
    const std::string message = led.message();
    EXPECT_NE(message.find("LED0 is ON, expected OFF"), std::string::npos) << message;
    EXPECT_NE(message.find("at t = 250 cycles (0.002500 ms)"), std::string::npos) << message;
    EXPECT_NE(message.find("write32 FIO1SET   0x10000000"), std::string::npos) << message;
    EXPECT_NE(message.find("led     LED0      ON"), std::string::npos) << message;

    const testing::AssertionResult pin = s.pin(1, 28, Level::Low);
    ASSERT_FALSE(pin);
    EXPECT_NE(std::string(pin.message()).find("P1.28 is high, expected low"), std::string::npos);

    const testing::AssertionResult reg = s.reg(FIO1DIR, 0);
    ASSERT_FALSE(reg);
    EXPECT_NE(std::string(reg.message()).find("register 0x2009C020 is 0xB0000000, expected 0x00000000"),
              std::string::npos);

    EXPECT_EQ(s.mcu().trace().events().size(), events);
    EXPECT_EQ(s.now(), Cycles{250});
    EXPECT_TRUE(s.led(0, LedState::On));
}

TEST(Scenario, RepeatedScenariosAreIdentical) {
    auto run = [] {
        Scenario s;
        KeilBoardLed leds(s.mcu());
        start_blinker(s, leds);
        s.run_until(3ms);
        s.press(JoystickDirection::Down);
        s.run_until(7500us);
        s.release(JoystickDirection::Down);
        return std::pair{s.mcu().trace().events(), s.now()};
    };
    EXPECT_EQ(run(), run());
}
