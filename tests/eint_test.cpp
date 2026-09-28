// EINT0 on P2.10 (lpc17xx/eint.hpp): the MCB1700 INT0 button through the external
// interrupt block and the NVIC.
#include "host/eint0_counter.h"

#include "boards/mcb1700/board.hpp"
#include "host/binding.hpp"
#include "host/c/latasim_keil_gpio.h"
#include "lpc17xx/lpc1768.hpp"
#include "lpc17xx/nvic.hpp"
#include "scenario.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std::chrono_literals;
using latasim::lpc17xx::kEint0Irq;
using latasim::lpc17xx::kTimer0Irq;
using latasim::lpc17xx::Lpc1768;
using latasim::mcb1700::Board;
using latasim::mcb1700::LedState;
using latasim::test::Scenario;

namespace {

constexpr std::uint32_t EXTINT = 0x400FC140;
constexpr std::uint32_t EXTMODE = 0x400FC148;
constexpr std::uint32_t EXTPOLAR = 0x400FC14C;
constexpr std::uint32_t PINSEL4 = 0x4002C010;
constexpr std::uint32_t ISER0 = 0xE000E100;

void select_eint0(Lpc1768& mcu) { mcu.write32(PINSEL4, 1u << 20); }

}  // namespace

TEST(Eint, ResetState) {
    Lpc1768 mcu;
    EXPECT_EQ(mcu.read32(EXTINT), 0u);
    EXPECT_EQ(mcu.read32(EXTMODE), 0u) << "level";
    EXPECT_EQ(mcu.read32(EXTPOLAR), 0u) << "low-active";
    EXPECT_EQ(mcu.read32(PINSEL4), 0u) << "P2.10 is GPIO at reset";
}

TEST(Eint, ReleasedButtonRaisesNothing) {
    Board board;
    select_eint0(board.mcu());
    board.mcu().advance_cycles(1000);
    EXPECT_EQ(board.mcu().read32(EXTINT), 0u) << "released = high: inactive";
}

TEST(Eint, OnlyTheEint0PinFunctionSeesTheButton) {
    Board board;
    board.press_int0();  // P2.10 as GPIO: the button is only a GPIO level
    EXPECT_EQ(board.mcu().read32(EXTINT), 0u);
    EXPECT_FALSE(board.mcu().nvic().pending(kEint0Irq));
}

TEST(Eint, LevelModeHoldsTheFlagWhileThePinIsActive) {
    Board board;
    auto& mcu = board.mcu();
    select_eint0(mcu);
    board.press_int0();
    EXPECT_EQ(mcu.read32(EXTINT), 1u);
    EXPECT_TRUE(mcu.nvic().pending(kEint0Irq)) << "pending even while disabled";
    mcu.write32(EXTINT, 1);
    EXPECT_EQ(mcu.read32(EXTINT), 1u) << "cannot be cleared while the pin is low";
    board.release_int0();
    EXPECT_EQ(mcu.read32(EXTINT), 1u) << "stays set until cleared";
    mcu.write32(EXTINT, 1);
    EXPECT_EQ(mcu.read32(EXTINT), 0u);
}

TEST(Eint, FallingEdgeModeFlagsOncePerPress) {
    Board board;
    auto& mcu = board.mcu();
    select_eint0(mcu);
    mcu.write32(EXTMODE, 1);  // edge, falling
    mcu.write32(EXTINT, 1);
    board.press_int0();
    EXPECT_EQ(mcu.read32(EXTINT), 1u);
    mcu.write32(EXTINT, 1);
    EXPECT_EQ(mcu.read32(EXTINT), 0u) << "clearable while held";
    mcu.advance_cycles(10'000);
    EXPECT_EQ(mcu.read32(EXTINT), 0u) << "holding is not another edge";
    board.release_int0();
    EXPECT_EQ(mcu.read32(EXTINT), 0u) << "rising edge ignored";
}

TEST(Eint, RisingEdgeModeFlagsOnRelease) {
    Board board;
    auto& mcu = board.mcu();
    select_eint0(mcu);
    mcu.write32(EXTMODE, 1);
    mcu.write32(EXTPOLAR, 1);  // rising
    board.press_int0();
    EXPECT_EQ(mcu.read32(EXTINT), 0u);
    board.release_int0();
    EXPECT_EQ(mcu.read32(EXTINT), 1u);
}

TEST(Eint, HighLevelModeIsActiveWhileReleased) {
    Board board;
    auto& mcu = board.mcu();
    select_eint0(mcu);
    mcu.write32(EXTPOLAR, 1);  // level, high-active: the released button is active
    EXPECT_EQ(mcu.read32(EXTINT), 1u);
}

TEST(Eint, ButtonPressesReachTheHandler) {
    Scenario s;
    s.bind(kEint0Irq, EINT0_IRQHandler);
    eint0_counter_start(true, false);  // falling edge
    EXPECT_EQ(eint0_count, 0u);
    for (int press = 1; press <= 5; ++press) {
        s.run_for(5ms);
        s.press_int0();
        EXPECT_EQ(eint0_count, static_cast<std::uint32_t>(press));
        s.run_for(5ms);
        s.release_int0();
    }
    EXPECT_TRUE(s.led(7, LedState::On)) << "odd count: LED7 on";
    const auto& events = s.mcu().trace().events();
    std::vector<std::string> last;
    for (auto it = events.end() - 8; it != events.end() - 1; ++it) last.push_back(to_string(*it).substr(6));  // before the last release
    EXPECT_EQ(last, (std::vector<std::string>{
                        "t=4500000    input   P2.10     low",
                        "t=4500000    irq     EINT0     pend",
                        "t=4500000    irq     EINT0     enter",
                        "t=4500000    write32 EXTINT    0x00000001",
                        "t=4500000    write32 FIO2SET   0x00000040",
                        "t=4500000    led     LED7      ON",
                        "t=4500000    irq     EINT0     exit",
                    }));
}

TEST(Eint, DisabledInterruptWaitsInTheFlag) {
    Scenario s;
    s.bind(kEint0Irq, EINT0_IRQHandler);
    eint0_counter_start(true, false);
    s.mcu().write32(0xE000E180, 1u << kEint0Irq);  // ICER: disable
    s.press_int0();
    EXPECT_EQ(eint0_count, 0u);
    EXPECT_EQ(s.mcu().read32(EXTINT), 1u);
    s.mcu().write32(ISER0, 1u << kEint0Irq);  // enable: taken at once
    EXPECT_EQ(eint0_count, 1u);
}

// Level mode with a handler that cannot clear the flag while the button is held:
// the interrupt is taken again and again, as on the chip, until it is an error.
TEST(Eint, LevelModeHeldButtonIsAnInterruptStorm) {
    Scenario s;
    s.bind(kEint0Irq, EINT0_IRQHandler);
    eint0_counter_start(false, false);  // level, low
    EXPECT_THROW(s.press_int0(), std::logic_error);
}

// EINT0 and TIMER0 both pending while disabled in the NVIC (SysTick, which has no
// enable, is taken on its own); one ISER write then enables both and priority
// decides which handler runs first, either way round.
TEST(Eint, CompetesWithTimerAndSysTickByPriority) {
    for (const bool eint_first : {true, false}) {
        Board board;
        auto& mcu = board.mcu();
        std::vector<std::string> order;
        mcu.bind_handler(kEint0Irq, [&] {
            order.push_back("EINT0");
            mcu.write32(EXTINT, 1);
        });
        mcu.bind_handler(kTimer0Irq, [&] {
            order.push_back("TIMER0");
            mcu.write32(0x40004000, 1);  // T0IR
        });
        mcu.bind_handler(latasim::lpc17xx::kSysTickIrq, [&] { order.push_back("SysTick"); });
        mcu.write8(0xE000E400 + kEint0Irq, eint_first ? 0 : 2 << 3);
        mcu.write8(0xE000E400 + kTimer0Irq, eint_first ? 2 << 3 : 0);
        select_eint0(mcu);
        mcu.write32(EXTMODE, 1);      // falling edge
        board.press_int0();           // EINT0 pending, disabled
        mcu.write32(0x40004018, 99);  // T0MR0: TIMER0 flag at t = 400
        mcu.write32(0x40004014, 1);
        mcu.write32(0x40004004, 1);
        mcu.write32(0xE000E014, 400);  // SysTick at t = 400 as well
        mcu.write32(0xE000E010, 7);
        mcu.advance_cycles(400);
        EXPECT_EQ(order, (std::vector<std::string>{"SysTick"}));
        EXPECT_TRUE(mcu.nvic().pending(kEint0Irq));
        EXPECT_TRUE(mcu.nvic().pending(kTimer0Irq));
        order.clear();
        mcu.write32(ISER0, (1u << kEint0Irq) | (1u << kTimer0Irq));
        EXPECT_EQ(order, eint_first ? (std::vector<std::string>{"EINT0", "TIMER0"})
                                    : (std::vector<std::string>{"TIMER0", "EINT0"}));
    }
}

TEST(Eint, KeilPinConfigureSelectsEint0) {
    Board board;
    latasim::host::FirmwareBinding bind(board);
    PIN_Configure(2, 10, 1, 0, 0);  // PIN_FUNC_1: EINT0
    EXPECT_EQ(board.mcu().peek32(PINSEL4), 1u << 20);
    board.press_int0();
    EXPECT_EQ(board.mcu().peek32(EXTINT), 1u);
}

TEST(Eint, RepeatedRunsAreIdentical) {
    auto run = [] {
        Scenario s;
        s.bind(kEint0Irq, EINT0_IRQHandler);
        eint0_counter_start(true, false);
        for (int i = 0; i < 4; ++i) {
            s.run_for(3ms);
            s.press_int0();
            s.run_for(2ms);
            s.release_int0();
        }
        return s.mcu().trace().events();
    };
    EXPECT_EQ(run(), run());
}
