// Keil's Blinky_ULp example on virtual time: IRQ.c's SysTick_Handler, compiled
// unchanged, steps an LED chase every 10 ms (docs/phase3/timed-firmware.md).
//
// IRQ.c keeps its state in function-static variables, and nothing re-runs C
// startup between runs in one process, so a test that runs the firmware twice
// runs whole 2 s spans: 200 ticks bring both the 8-step chase and the 100-tick
// seconds counter back to where they started.
#include "firmware/blinky_ulp.h"

#include "boards/mcb1700/board.hpp"
#include "gpio_snapshot.hpp"
#include "host/binding.hpp"
#include "lpc17xx/lpc1768.hpp"
#include "trace/trace.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

using latasim::TraceEvent;
using latasim::TraceKind;
using latasim::host::FirmwareBinding;
using latasim::lpc17xx::kCyclesPerMillisecond;
using latasim::mcb1700::Board;
using latasim::mcb1700::kLedCount;
using latasim::mcb1700::LedState;
using latasim::test::snapshot;

namespace {

constexpr std::uint64_t kTick = 10 * kCyclesPerMillisecond;  // SysTick_Config(SystemCoreClock / 100)

// Blinky_ULp on a fresh board: Blinky.c's start-up, then SysTick_Handler on each tick.
struct Blinky {
    Board board;
    FirmwareBinding bind{board};
    Blinky() {
        blinky_ulp_start();
        board.mcu().on_systick(SysTick_Handler);
    }
    void run(std::uint64_t cycles) { board.mcu().advance_cycles(cycles); }
    // The one LED on, or -1 if none or several.
    int lit() const {
        int on = -1;
        for (unsigned i = 0; i < kLedCount; ++i) {
            if (board.led(i) != LedState::On) continue;
            if (on != -1) return -1;
            on = static_cast<int>(i);
        }
        return on;
    }
};

}  // namespace

TEST(BlinkyUlp, StartUpConfiguresLedsAndA10msSysTick) {
    Blinky blinky;
    for (unsigned i = 0; i < kLedCount; ++i) EXPECT_EQ(blinky.board.led(i), LedState::Off);
    const auto& systick = blinky.board.mcu().systick();
    EXPECT_EQ(systick.reload(), 999'999u) << "SysTick_Config(SystemCoreClock / 100)";
    EXPECT_EQ(systick.ctrl(), 0x7u) << "CLKSOURCE | TICKINT | ENABLE";
    EXPECT_EQ(blinky.board.mcu().cycles(), 0u) << "start-up takes no virtual time";
}

// SysTick_Config enables the counter at RELOAD = 999,999, so the first tick is
// 999,999 cycles later and the rest follow every 1,000,000, as in the simulator (E11).
TEST(BlinkyUlp, FirstStepIsOneTickMinusOneCycleAfterStartUp) {
    Blinky blinky;
    blinky.run(kTick - 2);
    EXPECT_EQ(blinky.lit(), -1) << "all off until the first tick";
    blinky.run(1);
    EXPECT_EQ(blinky.lit(), 1) << "the chase starts by shifting LED0's bit to LED1";
    EXPECT_EQ(blinky.board.mcu().cycles(), kTick - 1);
}

TEST(BlinkyUlp, EachTickLightsTheNextLedAndWrapsAfterLed7) {
    Blinky blinky;
    std::vector<int> seen;
    for (int tick = 1; tick <= 200; ++tick) {
        blinky.run(kTick);
        seen.push_back(blinky.lit());
    }
    for (int tick = 1; tick <= 200; ++tick) EXPECT_EQ(seen[tick - 1], tick % 8) << "tick " << tick;
}

TEST(BlinkyUlp, NothingChangesBetweenTicks) {
    Blinky blinky;
    blinky.run(kTick);  // tick 1 at kTick - 1
    const auto events = blinky.board.mcu().trace().events().size();
    blinky.run(kTick - 2);  // tick 2 is at 2 * kTick - 1
    EXPECT_EQ(blinky.board.mcu().trace().events().size(), events);
    EXPECT_EQ(blinky.lit(), 1);
}

// LED_SetOut(0x04) at the second tick: one SET or CLR store per LED, in LED
// order, each followed by the LED change it causes.
TEST(BlinkyUlp, TickTraceIsTheHandlersRegisterTrafficInOrder) {
    Blinky blinky;
    blinky.run(kTick);
    const auto before = blinky.board.mcu().trace().events().size();
    blinky.run(kTick);
    const auto& all = blinky.board.mcu().trace().events();
    const std::vector<TraceEvent> tick(all.begin() + static_cast<std::ptrdiff_t>(before), all.end());
    std::vector<std::string> lines;
    for (const auto& e : tick) lines.push_back(to_string(e).substr(6));  // without "#seq  "
    EXPECT_EQ(lines, (std::vector<std::string>{
                         "write32 FIO1CLR   0x10000000",  // LED0 (P1.28), already off
                         "write32 FIO1CLR   0x20000000",  // LED1 (P1.29)
                         "led     LED1      OFF",
                         "write32 FIO1SET   0x80000000",  // LED2 (P1.31)
                         "led     LED2      ON",
                         "write32 FIO2CLR   0x00000004",  // LED3-LED7 (P2.2-P2.6)
                         "write32 FIO2CLR   0x00000008",
                         "write32 FIO2CLR   0x00000010",
                         "write32 FIO2CLR   0x00000020",
                         "write32 FIO2CLR   0x00000040",
                     }));
}

TEST(BlinkyUlp, SecondsFlagIsSetEvery100Ticks) {
    Blinky blinky;
    clock_1s = 0;  // Blinky.c's main loop consumes it; the host plays that part
    blinky.run(100 * kTick - 2);
    EXPECT_EQ(clock_1s, 0);
    blinky.run(1);
    EXPECT_EQ(clock_1s, 1) << "on tick 100, 1 s minus one cycle after start-up";
}

TEST(BlinkyUlp, RepeatedRunsAreIdentical) {
    auto run = [] {
        Blinky blinky;
        blinky.run(200 * kTick);  // whole 2 s: IRQ.c's statics end where they began
        return std::tuple{blinky.board.mcu().trace().events(), snapshot(blinky.board), blinky.board.mcu().cycles()};
    };
    const auto first = run();
    const auto second = run();
    EXPECT_EQ(std::get<0>(first), std::get<0>(second)) << "trace";
    EXPECT_EQ(std::get<1>(first), std::get<1>(second)) << "GPIO and LED state";
    EXPECT_EQ(std::get<2>(first), 200 * kTick);
    EXPECT_EQ(std::get<2>(second), 200 * kTick);
}
