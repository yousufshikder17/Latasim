// Phase 4's integrated scenario: one board running Keil's Blinky_ULp (SysTick and
// its ADC with Keil's ADC_IRQHandler), Keil's GLCD driver, the representative
// EINT0 handler for the INT0 button and a TIMER0 interrupt, all through one NVIC.
// The application's main loop is Blinky.c's (take the ADC value) plus showing the
// INT0 press count on the display.
#include "blinky_ulp_fixture.hpp"
#include "host/eint0_counter.h"
#include "scenario.hpp"

extern "C" {  // Keil's header has no C++ guards; the driver is compiled as C
#include "Board_GLCD.h"
extern GLCD_FONT GLCD_Font_16x24;
}

#include "LPC17xx.h"
#include "lpc17xx/nvic.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using namespace std::chrono_literals;
using latasim::lpc17xx::kAdcIrq;
using latasim::lpc17xx::kEint0Irq;
using latasim::lpc17xx::kSysTickIrq;
using latasim::lpc17xx::kTimer0Irq;
using latasim::mcb1700::LedState;
using latasim::test::Scenario;

namespace {

std::uint32_t timer0_ticks;
std::uint32_t shown_presses;

extern "C" void app_TIMER0_IRQHandler(void) {
    LPC_TIM0->IR = 1U;
    ++timer0_ticks;
}

// The application's main loop body: Blinky.c's ADC step, then the display.
void app_main_loop_step() {
    blinky_ulp_main_loop_step();
    if (eint0_count != shown_presses) {
        shown_presses = eint0_count;
        char text[16];
        std::snprintf(text, sizeof text, "INT0: %u", static_cast<unsigned>(shown_presses));
        GLCD_DrawString(8, 8, text);
    }
}

struct App {
    Scenario s;
    App() {
        timer0_ticks = 0;
        shown_presses = 0;
        s.board().set_potentiometer(0x300);  // chase every 4 ticks
        blinky_ulp_start();
        GLCD_Initialize();
        GLCD_SetBackgroundColor(0xFFFF);
        GLCD_SetForegroundColor(0x001F);
        GLCD_SetFont(&GLCD_Font_16x24);
        GLCD_ClearScreen();
        latasim::test::bind_blinky(s.mcu());
        s.mcu().on_thread_mode(app_main_loop_step);
        s.bind(kEint0Irq, EINT0_IRQHandler);
        s.bind(kTimer0Irq, app_TIMER0_IRQHandler);
        eint0_counter_start(true, false);  // falling edge: one interrupt per press
        // TIMER0 at CCLK, flagging every 999,999 cycles: its first match lands on
        // the same cycle as SysTick's first count to 0 (t = 999,999).
        LPC_SC->PCLKSEL0 = (LPC_SC->PCLKSEL0 & ~(3UL << 2)) | (1UL << 2);
        LPC_TIM0->MR0 = 999'998U;
        LPC_TIM0->MCR = 3U;
        NVIC_EnableIRQ(TIMER0_IRQn);
        LPC_TIM0->TCR = 3U;
        LPC_TIM0->TCR = 1U;
    }
};

std::vector<std::string> interrupts_at(const Scenario& s, std::uint64_t cycles) {
    std::vector<std::string> out;
    for (const auto& e : const_cast<Scenario&>(s).mcu().trace().events())
        if (e.cycles == cycles && e.kind == latasim::TraceKind::Interrupt) out.push_back(to_string(e).substr(27));
    return out;
}

}  // namespace

TEST(Integration, SimultaneousTimerAndSysTickAreTakenByPriority) {
    latasim::test::reset_irq_statics();
    App app;
    EXPECT_EQ(app.s.mcu().nvic().priority(kSysTickIrq), 0xF8u) << "SysTick_Config: lowest priority";
    EXPECT_EQ(app.s.mcu().nvic().priority(kTimer0Irq), 0u);
    app.s.run_until(11ms);
    // Both pend on the same cycle; TIMER0 (priority 0) is taken first, SysTick
    // (31, the lowest) tail-chains after it.
    EXPECT_EQ(interrupts_at(app.s, 999'999),
              (std::vector<std::string>{"SysTick   pend", "TIMER0    pend", "TIMER0    enter", "TIMER0    exit",
                                        "SysTick   enter", "SysTick   exit"}));
    EXPECT_EQ(interrupts_at(app.s, 1'001'299),
              (std::vector<std::string>{"ADC       pend", "ADC       enter", "ADC       exit"}))
        << "the conversion SysTick_Handler started";
    EXPECT_EQ(timer0_ticks, 1u);
    EXPECT_EQ(AD_last, 0x300u) << "main loop step took the ADC value";
}

TEST(Integration, ButtonTimerAdcSysTickAndDisplayTogether) {
    latasim::test::reset_irq_statics();
    App app;
    Scenario& s = app.s;
    s.run_until(25ms);
    s.press_int0();  // EINT0 handler, then the main loop shows the count
    s.run_until(30ms);
    s.release_int0();
    s.run_until(55ms);
    s.press_int0();
    s.run_until(100ms);

    EXPECT_EQ(eint0_count, 2u);
    EXPECT_EQ(shown_presses, 2u) << "the display was updated after each press";
    EXPECT_EQ(timer0_ticks, 10u) << "every 999,999 cycles for 100 ms";
    // Chase at 0x300: LED1 at tick 1, then every 4 ticks: 5, 9 (tick 9 at 89.99 ms).
    EXPECT_TRUE(s.led(3, LedState::On));
    // The screen: "INT0: 2" in the 16x24 font starting at (8, 8); ink in the cell.
    unsigned ink = 0;
    for (unsigned y = 8; y < 32; ++y)
        for (unsigned x = 8; x < 8 + 7 * 16; ++x) ink += s.board().glcd().pixel(x, y) == 0x001F;
    EXPECT_GT(ink, 100u);
    EXPECT_EQ(s.board().glcd().pixel(300, 200), 0xFFFFu) << "rest of the screen still clear";
}

TEST(Integration, RepeatedRunsAreIdentical) {
    auto run = [] {
        latasim::test::reset_irq_statics();
        App app;
        app.s.run_until(12ms);
        app.s.press_int0();
        app.s.run_until(40ms);
        return std::pair{app.s.mcu().trace().events(), app.s.board().glcd().hash()};
    };
    const auto first = run();
    const auto second = run();
    EXPECT_EQ(first.first, second.first) << "trace";
    EXPECT_EQ(first.second, second.second) << "display";
}
