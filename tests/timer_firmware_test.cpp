// Register-level TIMER0 firmware (host/timer0_blink.cpp) on virtual time.
#include "host/timer0_blink.h"

#include "lpc17xx/nvic.hpp"
#include "scenario.hpp"

#include <gtest/gtest.h>

#include <chrono>

using namespace std::chrono_literals;
using latasim::lpc17xx::kTimer0Irq;
using latasim::mcb1700::LedState;
using latasim::test::Scenario;

TEST(TimerFirmware, TogglesLed0AtTheTimerRate) {
    Scenario s;
    s.bind(kTimer0Irq, TIMER0_IRQHandler);
    timer0_blink_start(1000);  // 1 kHz: MR0 = 99,999 at PCLK = CCLK
    EXPECT_TRUE(s.led(0, LedState::Off));
    EXPECT_TRUE(s.reg(0x40004018, 99'999)) << "T0MR0";
    s.run_until(1ms);  // TC reaches MR0 at 99,999 cycles; the flag one edge later
    EXPECT_TRUE(s.led(0, LedState::On));
    EXPECT_EQ(timer0_blink_ticks, 1u);
    s.run_until(2ms);
    EXPECT_TRUE(s.led(0, LedState::Off));
    s.run_until(1s);
    EXPECT_EQ(timer0_blink_ticks, 1000u);
    EXPECT_TRUE(s.reg(0x40004000, 0)) << "T0IR cleared by the handler";
}

TEST(TimerFirmware, RepeatedRunsAreIdentical) {
    auto run = [] {
        Scenario s;
        s.bind(kTimer0Irq, TIMER0_IRQHandler);
        timer0_blink_start(2500);
        s.run_until(7ms);
        return s.mcu().trace().events();
    };
    EXPECT_EQ(run(), run());
}
