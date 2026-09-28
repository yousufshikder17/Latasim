// A real-firmware scenario: Keil's Blinky_ULp steps its LED chase every 10 ms of
// virtual time while the application reads the joystick through Keil's joystick
// driver. Timed output and timed input, both from unmodified Keil sources.
#include "blinky_ulp_fixture.hpp"
#include "scenario.hpp"

extern "C" {  // Keil's headers have no C++ guards; the drivers are compiled as C
#include "Board_Joystick.h"
}

#include <gtest/gtest.h>

#include <chrono>

using namespace std::chrono_literals;
using latasim::mcb1700::JoystickDirection;
using latasim::mcb1700::LedState;
using latasim::test::Level;
using latasim::test::Scenario;

TEST(KeilScenario, LedChaseOverTimeWithAJoystickPressInTheMiddle) {
    latasim::test::reset_irq_statics();
    Scenario s;
    blinky_ulp_start();
    Joystick_Initialize();
    s.on_systick(SysTick_Handler);

    // Ticks fall 1 cycle before each 10 ms mark (SysTick loads RELOAD on enable).
    EXPECT_TRUE(s.led(0, LedState::Off));
    s.run_until(10ms);
    EXPECT_TRUE(s.led(1, LedState::On));
    s.run_until(25ms);
    EXPECT_TRUE(s.led(2, LedState::On));
    EXPECT_TRUE(s.led(1, LedState::Off));

    // At T = 25 ms the joystick is pushed up; the firmware sees it on its next read.
    s.press(JoystickDirection::Up);
    EXPECT_TRUE(s.pin(1, 23, Level::Low));
    EXPECT_EQ(Joystick_GetState(), static_cast<uint32_t>(JOYSTICK_UP));

    // The chase is unaffected; at T = 50 ms the joystick is released.
    s.run_until(50ms);
    EXPECT_TRUE(s.led(5, LedState::On));
    s.release(JoystickDirection::Up);
    EXPECT_TRUE(s.pin(1, 23, Level::High));
    EXPECT_EQ(Joystick_GetState(), 0u);

    // Tick 8 wraps the chase back to LED0.
    s.run_until(80ms);
    EXPECT_TRUE(s.led(0, LedState::On));
    EXPECT_TRUE(s.led(7, LedState::Off));
    EXPECT_TRUE(s.reg(0x2009C034, 0x5FFFC713)) << "FIO1PIN: LED0 on, inputs released";
}
