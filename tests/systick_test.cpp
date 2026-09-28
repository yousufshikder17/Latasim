// SysTick through the MMIO path (Lpc1768), plus its closed-form counting checked
// against a clock-by-clock reference.
#include "boards/mcb1700/board.hpp"
#include "lpc17xx/lpc1768.hpp"
#include "lpc17xx/systick.hpp"
#include "trace/trace.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>
#include <vector>

using latasim::TraceKind;
using latasim::lpc17xx::BusFault;
using latasim::lpc17xx::kCyclesPerMillisecond;
using latasim::lpc17xx::kSysTickCalib;
using latasim::lpc17xx::kSysTickClksource;
using latasim::lpc17xx::kSysTickCountflag;
using latasim::lpc17xx::kSysTickEnable;
using latasim::lpc17xx::kSysTickTickint;
using latasim::lpc17xx::Lpc1768;
using latasim::lpc17xx::SysTick;
using latasim::lpc17xx::SysTickReg;

namespace {

// Addresses as firmware spells them (CMSIS SysTick->CTRL etc.).
constexpr std::uint32_t STCTRL = 0xE000E010;
constexpr std::uint32_t STRELOAD = 0xE000E014;
constexpr std::uint32_t STCURR = 0xE000E018;
constexpr std::uint32_t STCALIB = 0xE000E01C;
constexpr std::uint32_t kRun = kSysTickClksource | kSysTickEnable;

// What CMSIS SysTick_Config(ticks) writes, minus its NVIC priority store.
void configure(Lpc1768& mcu, std::uint32_t ticks, std::uint32_t ctrl = kRun) {
    mcu.write32(STRELOAD, ticks - 1);
    mcu.write32(STCURR, 0);
    mcu.write32(STCTRL, ctrl);
}

// The per-clock rule, one clock at a time: ARM DUI 0552A 4.4.
struct Reference {
    std::uint32_t reload, current;
    bool countflag = false;
    std::uint64_t zeros = 0;
    void clock() {
        if (current == 0) {
            current = reload;
        } else if (--current == 0) {
            countflag = true;
            ++zeros;
        }
    }
};

}  // namespace

TEST(SysTick, ResetStateMatchesUm10360) {
    Lpc1768 mcu;
    EXPECT_EQ(mcu.read32(STCTRL), 0x00000004u) << "CLKSOURCE = CPU clock, disabled";
    EXPECT_EQ(mcu.read32(STRELOAD), 0u);
    EXPECT_EQ(mcu.read32(STCURR), 0u);
    EXPECT_EQ(mcu.read32(STCALIB), 0x000F423Fu) << "TENMS: 10 ms at 100 MHz";
    EXPECT_EQ(kSysTickCalib + 1, 10 * kCyclesPerMillisecond);
}

TEST(SysTick, LoadKeepsTwentyFourBits) {
    Lpc1768 mcu;
    mcu.write32(STRELOAD, 999'999);
    EXPECT_EQ(mcu.read32(STRELOAD), 999'999u);
    mcu.write32(STRELOAD, 0xFFFFFFFF);
    EXPECT_EQ(mcu.read32(STRELOAD), 0x00FFFFFFu) << "bits 31:24 reserved";
}

TEST(SysTick, CtrlKeepsOnlyItsControlBits) {
    Lpc1768 mcu;
    mcu.write32(STCTRL, 0xFFFFFFFF);
    EXPECT_EQ(mcu.read32(STCTRL), kSysTickClksource | kSysTickTickint | kSysTickEnable)
        << "COUNTFLAG is read-only and reserved bits read 0";
}

TEST(SysTick, DisabledCounterDoesNotMove) {
    Lpc1768 mcu;
    mcu.write32(STRELOAD, 99);
    mcu.write32(STCTRL, kSysTickClksource);  // not enabled
    mcu.advance_cycles(1'000);
    EXPECT_EQ(mcu.read32(STCURR), 0u);
    EXPECT_EQ(mcu.read32(STCTRL) & kSysTickCountflag, 0u);
    EXPECT_EQ(mcu.cycles(), 1'000u) << "time still passes";
}

TEST(SysTick, ExternalClockSourceIsNotModelledSoTheCounterStops) {
    Lpc1768 mcu;
    configure(mcu, 100, kSysTickEnable);  // CLKSOURCE = 0: STCLK pin, not modelled
    mcu.advance_cycles(1'000);
    EXPECT_EQ(mcu.read32(STCURR), 99u) << "loaded on enable, then no clock edges";
}

TEST(SysTick, EnablingLoadsReloadThenEachClockDecrements) {
    Lpc1768 mcu;
    configure(mcu, 100);  // RELOAD 99
    EXPECT_EQ(mcu.read32(STCURR), 99u) << "loaded when ENABLE was set (ARM 4.4.1, E11)";
    mcu.advance_cycles(1);
    EXPECT_EQ(mcu.read32(STCURR), 98u);
    mcu.advance_cycles(97);
    EXPECT_EQ(mcu.read32(STCURR), 1u);
    EXPECT_EQ(mcu.read32(STCTRL) & kSysTickCountflag, 0u) << "not yet at 0";
}

// E12: re-enabling with STCURR = 989 and RELOAD = 500 loaded 500.
TEST(SysTick, EnablingLoadsReloadEvenMidCount) {
    Lpc1768 mcu;
    configure(mcu, 1'000);
    mcu.advance_cycles(10);
    mcu.write32(STCTRL, kSysTickClksource);  // stop
    ASSERT_EQ(mcu.read32(STCURR), 989u);
    mcu.write32(STRELOAD, 500);
    mcu.write32(STCTRL, kRun);
    EXPECT_EQ(mcu.read32(STCURR), 500u);
    mcu.write32(STCTRL, kRun);  // already enabled: no reload
    EXPECT_EQ(mcu.read32(STCURR), 500u);
}

TEST(SysTick, CountingToZeroSetsCountflagAndTheNextClockReloads) {
    Lpc1768 mcu;
    configure(mcu, 100);
    mcu.advance_cycles(99);  // 99 clocks from 99 down to 0
    EXPECT_EQ(mcu.read32(STCURR), 0u);
    EXPECT_EQ(mcu.read32(STCTRL) & kSysTickCountflag, kSysTickCountflag);
    mcu.advance_cycles(1);
    EXPECT_EQ(mcu.read32(STCURR), 99u) << "wrapped to RELOAD";
}

TEST(SysTick, ReadingCtrlClearsCountflag) {
    Lpc1768 mcu;
    configure(mcu, 10);
    mcu.advance_cycles(10);
    EXPECT_TRUE(mcu.systick().countflag()) << "observing without MMIO leaves it set";
    EXPECT_EQ(mcu.read32(STCTRL) & kSysTickCountflag, kSysTickCountflag);
    EXPECT_EQ(mcu.read32(STCTRL) & kSysTickCountflag, 0u);
}

TEST(SysTick, WritingValClearsCounterAndCountflag) {
    Lpc1768 mcu;
    configure(mcu, 10);
    mcu.advance_cycles(15);
    ASSERT_NE(mcu.read32(STCURR), 0u);
    mcu.write32(STCURR, 0x12345678);  // any value
    EXPECT_EQ(mcu.read32(STCURR), 0u);
    EXPECT_EQ(mcu.read32(STCTRL) & kSysTickCountflag, 0u);
}

TEST(SysTick, ReloadZeroNeverCountsToZero) {
    Lpc1768 mcu;
    configure(mcu, 1);  // RELOAD 0
    mcu.advance_cycles(1'000);
    EXPECT_EQ(mcu.read32(STCURR), 0u);
    EXPECT_EQ(mcu.read32(STCTRL) & kSysTickCountflag, 0u);
}

// E11: the simulator's counter reached 0 999,999 cycles after SysTick_Config
// enabled it, then every 1,000,000 cycles (10 ms).
TEST(SysTick, TenMillisecondPeriodAtTheCoreClock) {
    Lpc1768 mcu;
    configure(mcu, 1'000'000);  // SysTick_Config(SystemCoreClock / 100)
    mcu.advance_cycles(999'998);
    EXPECT_EQ(mcu.read32(STCTRL) & kSysTickCountflag, 0u);
    mcu.advance_cycles(1);
    EXPECT_EQ(mcu.read32(STCTRL) & kSysTickCountflag, kSysTickCountflag) << "first: RELOAD cycles";
    mcu.advance_cycles(10 * kCyclesPerMillisecond - 1);
    EXPECT_EQ(mcu.read32(STCTRL) & kSysTickCountflag, 0u);
    mcu.advance_cycles(1);
    EXPECT_EQ(mcu.read32(STCTRL) & kSysTickCountflag, kSysTickCountflag) << "then every 10 ms";
}

TEST(SysTick, AdvanceReportsEveryCountToZero) {
    SysTick timer;
    timer.write(SysTickReg::Load, 9);  // period 10
    timer.write(SysTickReg::Ctrl, kRun);
    EXPECT_EQ(timer.advance(9), 1u);  // loaded 9 on enable
    EXPECT_EQ(timer.advance(10), 1u);
    EXPECT_EQ(timer.advance(35), 3u);
    EXPECT_EQ(timer.current(), 5u);
}

// The closed form agrees with the clock-by-clock rule for every split of time.
TEST(SysTick, ClosedFormMatchesClockByClockReference) {
    for (std::uint32_t reload : {0u, 1u, 2u, 7u, 100u}) {
        for (std::uint32_t start : {0u, 1u, 3u, 50u}) {
            for (std::uint64_t step : {1u, 2u, 5u, 13u, 250u}) {
                SysTick timer;
                timer.write(SysTickReg::Load, start);  // reach `start` by running
                timer.write(SysTickReg::Ctrl, kRun);    // loads `start`
                timer.write(SysTickReg::Load, reload);
                Reference ref{reload, timer.current()};
                std::uint64_t zeros = 0;
                for (int i = 0; i < 20; ++i) {
                    zeros += timer.advance(step);
                    for (std::uint64_t c = 0; c < step; ++c) ref.clock();
                    ASSERT_EQ(timer.current(), ref.current)
                        << "reload " << reload << " start " << start << " step " << step << " i " << i;
                    ASSERT_EQ(zeros, ref.zeros);
                    ASSERT_EQ(timer.countflag(), ref.countflag);
                }
            }
        }
    }
}

TEST(SysTick, OnlyWordAccessToItsFourRegistersIsMapped) {
    Lpc1768 mcu;
    EXPECT_THROW(mcu.write32(STCALIB, 0), BusFault) << "read-only";
    EXPECT_THROW(mcu.read8(STCTRL), BusFault);
    EXPECT_THROW(mcu.write16(STRELOAD, 1), BusFault);
    EXPECT_THROW(mcu.read32(STCTRL - 4), BusFault) << "SCS registers below SysTick";
    EXPECT_THROW(mcu.read32(STCALIB + 4), BusFault);
    EXPECT_THROW(mcu.read32(STCTRL + 2), BusFault) << "misaligned";
    EXPECT_EQ(mcu.read32(STRELOAD), 0u) << "failed accesses change nothing";
}

TEST(SysTick, RepeatedRunsAreIdentical) {
    auto run = [] {
        Lpc1768 mcu;
        configure(mcu, 1'000);
        std::vector<std::uint32_t> seen;
        for (std::uint64_t step : {1u, 999u, 1u, 12'345u, 7u}) {
            mcu.advance_cycles(step);
            seen.push_back(mcu.read32(STCURR));
            seen.push_back(mcu.read32(STCTRL));
        }
        seen.push_back(static_cast<std::uint32_t>(mcu.cycles()));
        return seen;
    };
    EXPECT_EQ(run(), run());
}

TEST(SysTick, AccessesAreTracedLikeAnyOtherRegister) {
    latasim::mcb1700::Board board;
    configure(board.mcu(), 100);
    const auto& events = board.mcu().trace().events();
    ASSERT_EQ(events.size(), 3u);
    EXPECT_EQ(events[0].kind, TraceKind::Write);
    EXPECT_EQ(events[0].address, STRELOAD);
    EXPECT_EQ(to_string(events[2]), "#3    t=0          write32 STCTRL    0x00000005");
    board.mcu().advance_cycles(50);
    EXPECT_EQ(events.size(), 3u) << "advancing time is not an MMIO access";
}

// --- SysTick_Handler delivery through the interrupt dispatch (Lpc1768::bind_handler) ---

TEST(SysTickHandler, CalledAtEachCountToZeroAtThatVirtualTime) {
    Lpc1768 mcu;
    std::vector<std::uint64_t> calls;
    mcu.bind_handler(latasim::lpc17xx::kSysTickIrq, [&] { calls.push_back(mcu.cycles()); });
    configure(mcu, 1'000, kRun | kSysTickTickint);
    mcu.advance_cycles(3'500);
    EXPECT_EQ(calls, (std::vector<std::uint64_t>{999, 1'999, 2'999}));
    EXPECT_EQ(mcu.cycles(), 3'500u);
    mcu.advance_cycles(498);
    EXPECT_EQ(calls.size(), 3u);
    mcu.advance_cycles(1);
    EXPECT_EQ(calls.back(), 3'999u) << "splitting the advance changes nothing";
}

TEST(SysTickHandler, EachCallIsTracedBeforeTheHandlersOwnAccesses) {
    latasim::mcb1700::Board board;
    auto& mcu = board.mcu();
    mcu.bind_handler(latasim::lpc17xx::kSysTickIrq, [&] { mcu.write32(0x2009C038, 1u << 28); });  // FIO1SET
    configure(mcu, 100, kRun | kSysTickTickint);
    mcu.advance_cycles(200);
    const auto& events = mcu.trace().events();
    ASSERT_EQ(events.size(), 3u + 8u);
    EXPECT_EQ(to_string(events[3]), "#4    t=99         irq     SysTick   pend");
    EXPECT_EQ(to_string(events[4]), "#5    t=99         irq     SysTick   enter");
    EXPECT_EQ(to_string(events[5]), "#6    t=99         write32 FIO1SET   0x10000000");
    EXPECT_EQ(to_string(events[6]), "#7    t=99         irq     SysTick   exit");
    EXPECT_EQ(to_string(events[7]), "#8    t=199        irq     SysTick   pend");
    EXPECT_EQ(events[10].cycles, 199u);
}

TEST(SysTickHandler, NotCalledWithoutTickint) {
    Lpc1768 mcu;
    int calls = 0;
    mcu.bind_handler(latasim::lpc17xx::kSysTickIrq, [&] { ++calls; });
    configure(mcu, 100);  // ENABLE | CLKSOURCE, no TICKINT
    mcu.advance_cycles(1'000);
    EXPECT_EQ(calls, 0);
    EXPECT_EQ(mcu.read32(STCTRL) & kSysTickCountflag, kSysTickCountflag) << "the counter still ran";
}

// With no handler bound, SysTick becomes pending and stays pending: it is never
// taken (Phase 3 only stored TICKINT in this case).
TEST(SysTickHandler, WithoutAHandlerTheExceptionStaysPending) {
    Lpc1768 mcu;
    configure(mcu, 100, kRun | kSysTickTickint);
    EXPECT_FALSE(mcu.nvic().pending(latasim::lpc17xx::kSysTickIrq));
    mcu.advance_cycles(1'000);
    EXPECT_TRUE(mcu.nvic().pending(latasim::lpc17xx::kSysTickIrq));
    EXPECT_FALSE(mcu.nvic().active(latasim::lpc17xx::kSysTickIrq));
    EXPECT_EQ(mcu.cycles(), 1'000u);
    EXPECT_EQ(mcu.read32(STCURR), 99u) << "counted to 0 at 99, 199, ... 999, then reloaded";
    EXPECT_EQ(mcu.read32(STCTRL) & kSysTickCountflag, kSysTickCountflag);
}

TEST(SysTickHandler, HandlerAccessesAreTracedAndSeeTheModel) {
    latasim::mcb1700::Board board;
    auto& mcu = board.mcu();
    mcu.write32(0x2009C020, 1u << 28);  // FIO1DIR: LED0 output
    mcu.bind_handler(latasim::lpc17xx::kSysTickIrq, [&] { mcu.write32(0x2009C038, 1u << 28); });  // FIO1SET
    configure(mcu, 100, kRun | kSysTickTickint);
    mcu.advance_cycles(98);
    EXPECT_EQ(board.led(0), latasim::mcb1700::LedState::Off);
    mcu.advance_cycles(1);
    EXPECT_EQ(board.led(0), latasim::mcb1700::LedState::On);
}

TEST(SysTickHandler, HandlerMayReconfigureTheTimer) {
    Lpc1768 mcu;
    std::vector<std::uint64_t> calls;
    mcu.bind_handler(latasim::lpc17xx::kSysTickIrq, [&] {
        calls.push_back(mcu.cycles());
        if (calls.size() == 2) mcu.write32(STCTRL, kRun);  // TICKINT off
    });
    configure(mcu, 10, kRun | kSysTickTickint);
    mcu.advance_cycles(100);
    EXPECT_EQ(calls, (std::vector<std::uint64_t>{9, 19}));
}

TEST(SysTickHandler, AdvancingTimeInsideTheHandlerIsAnError) {
    Lpc1768 mcu;
    mcu.bind_handler(latasim::lpc17xx::kSysTickIrq, [&] { mcu.advance_cycles(1); });
    configure(mcu, 10, kRun | kSysTickTickint);
    EXPECT_THROW(mcu.advance_cycles(10), std::logic_error);
    mcu.bind_handler(latasim::lpc17xx::kSysTickIrq, nullptr);
    mcu.advance_cycles(5);  // usable again after the failure
    EXPECT_EQ(mcu.cycles(), 14u) << "stopped at the count to 0 (cycle 9)";
}
