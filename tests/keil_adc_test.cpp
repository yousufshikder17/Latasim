// Keil's ADC_MCB1700.c (unchanged, from the pack) on the modelled ADC, and
// Blinky_ULp's LED chase following the potentiometer.
#include "blinky_ulp_fixture.hpp"

extern "C" {  // Keil's header has no C++ guards; the driver has C linkage
#include "Board_ADC.h"
}

#include "host/binding.hpp"
#include "lpc17xx/nvic.hpp"

#include <gtest/gtest.h>

#include <vector>

using latasim::lpc17xx::kAdcIrq;
using latasim::mcb1700::Board;
using latasim::test::Blinky;
using latasim::test::kBlinkyTick;

TEST(KeilAdc, InitializeConfiguresTheConverterAndItsInterrupt) {
    Board board;
    latasim::host::FirmwareBinding bind(board);
    ASSERT_EQ(ADC_Initialize(), 0);
    EXPECT_EQ(board.mcu().peek32(0x40034000), 0x00200404u) << "ADCR: AD0.2, CLKDIV 4, PDN";
    EXPECT_EQ(board.mcu().peek32(0x4003400C), 0x100u) << "ADINTEN: global DONE";
    EXPECT_TRUE(board.mcu().nvic().enabled(kAdcIrq));
    EXPECT_EQ(board.mcu().pconp() & (1u << 12), 1u << 12) << "PCADC";
    EXPECT_EQ(ADC_GetResolution(), 12u);
}

TEST(KeilAdc, ConversionCompletesThroughTheRealInterruptHandler) {
    latasim::test::reset_irq_statics();  // also leaves the driver's own statics clear
    Board board;
    latasim::host::FirmwareBinding bind(board);
    board.mcu().bind_handler(kAdcIrq, ADC_IRQHandler);
    ADC_Initialize();
    board.set_potentiometer(0x9A5);
    ADC_StartConversion();
    EXPECT_EQ(ADC_ConversionDone(), -1);
    EXPECT_EQ(ADC_GetValue(), -1);
    board.mcu().advance_cycles(1299);
    EXPECT_EQ(ADC_ConversionDone(), -1) << "13 us: 65 clocks at 5 MHz";
    board.mcu().advance_cycles(1);
    EXPECT_EQ(ADC_ConversionDone(), 0);
    EXPECT_EQ(ADC_GetValue(), 0x9A5);
    EXPECT_EQ(ADC_GetValue(), -1) << "consumed";
}

// IRQ.c steps the chase when (AD_last >> 8) ticks have passed since the last
// step: every tick at 0, every 9 at 0x800, every 16 at 0xFFF. The first step
// (tick 1) comes before any conversion has finished, with AD_last still 0.
TEST(KeilAdc, BlinkyChaseSpeedFollowsThePotentiometer) {
    struct Case {
        std::uint32_t pot;
        std::vector<int> steps;  // ticks at which the lit LED changes
    };
    for (const Case& c : {Case{0x000, {1, 2, 3, 4}}, Case{0x800, {1, 10, 19, 28}}, Case{0xFFF, {1, 17, 33, 49}}}) {
        Blinky blinky;
        blinky.board.set_potentiometer(c.pot);
        std::vector<int> steps;
        int lit = blinky.lit();
        for (int tick = 1; static_cast<int>(steps.size()) < 4 && tick <= 60; ++tick) {
            blinky.run(kBlinkyTick);
            if (blinky.lit() != lit) {
                steps.push_back(tick);
                lit = blinky.lit();
            }
        }
        EXPECT_EQ(steps, c.steps) << "potentiometer " << c.pot;
        EXPECT_EQ(AD_last, c.pot) << "Blinky.c's copy, via ADC_GetValue";
    }
}

TEST(KeilAdc, TurningThePotentiometerMidRunChangesTheSpeed) {
    Blinky blinky;
    blinky.run(5 * kBlinkyTick);  // fast: LED5 after 5 ticks
    EXPECT_EQ(blinky.lit(), 5);
    blinky.board.set_potentiometer(0x300);  // >> 8 = 3: every 4 ticks from now
    blinky.run(4 * kBlinkyTick);
    EXPECT_EQ(blinky.lit(), 6) << "tick 6 still used AD_last 0; then 3 quiet ticks";
    blinky.run(4 * kBlinkyTick);
    EXPECT_EQ(blinky.lit(), 7);
}
