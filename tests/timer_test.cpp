// LPC1768 Timer 0-3 (lpc17xx/timer.hpp) through the MMIO path, checked against an
// edge-by-edge reference of UM10360 chapter 21.
#include "boards/mcb1700/board.hpp"
#include "lpc17xx/lpc1768.hpp"
#include "lpc17xx/nvic.hpp"
#include "lpc17xx/timer.hpp"
#include "trace/trace.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

using latasim::lpc17xx::BusFault;
using latasim::lpc17xx::kCyclesPerMillisecond;
using latasim::lpc17xx::kSysTickIrq;
using latasim::lpc17xx::kTimer0Irq;
using latasim::lpc17xx::kTimerBase;
using latasim::lpc17xx::Lpc1768;
using latasim::lpc17xx::Timer;
using latasim::lpc17xx::TimerReg;

namespace {

enum : std::uint32_t { IR = 0x00, TCR = 0x04, TC = 0x08, PR = 0x0C, PC = 0x10, MCR = 0x14, MR0 = 0x18 };
constexpr std::uint32_t ISER0 = 0xE000E100;
constexpr std::uint32_t PCLKSEL0 = 0x400FC1A8;
constexpr std::uint32_t PCLKSEL1 = 0x400FC1AC;

std::uint32_t T(unsigned n, std::uint32_t reg) { return kTimerBase[n] + reg; }
std::uint32_t MR(unsigned n, unsigned ch) { return T(n, MR0 + 4 * ch); }

// UM10360 21.6.6-21.7, one PCLK edge at a time: the reference the closed form
// must agree with.
struct Reference {
    std::uint32_t ir = 0, tcr = 0, tc = 0, pr = 0, pc = 0, mcr = 0;
    std::array<std::uint32_t, 4> mr{};
    bool post = false, stop = false, reset_next = false;
    std::uint32_t flags = 0;

    bool counting() const { return (tcr & 1u) && !(tcr & 2u); }
    void match(std::uint32_t v) {
        flags = 0;
        stop = false;
        for (unsigned n = 0; n < 4; ++n) {
            const std::uint32_t bits = (mcr >> (3 * n)) & 7u;
            if (mr[n] != v || !bits) continue;
            post = true;
            if (bits & 1u) flags |= 1u << n;
            if (bits & 2u) reset_next = true;
            if (bits & 4u) stop = true;
        }
    }
    void count_one() {
        if (pc == pr) {
            pc = 0;
            tc = reset_next ? 0 : tc + 1;
            reset_next = false;
            match(tc);
        } else {
            ++pc;
        }
    }
    void edge() {
        if (post) {
            post = false;
            ir |= flags;
            if (stop) tcr &= ~1u;
            if (counting()) count_one();
            return;
        }
        if (counting()) count_one();
    }
};

}  // namespace

TEST(Timer, ResetState) {
    Lpc1768 mcu;
    for (unsigned n = 0; n < 4; ++n)
        for (std::uint32_t reg = IR; reg <= MR0 + 12; reg += 4) EXPECT_EQ(mcu.read32(T(n, reg)), 0u) << n << " " << reg;
    EXPECT_EQ(mcu.read32(PCLKSEL0), 0u);
    EXPECT_EQ(mcu.timer_divider(0), 4u) << "PCLK = CCLK/4 at reset: 25 MHz";
}

TEST(Timer, CountsPclkEdgesOnlyWhileEnabled) {
    Lpc1768 mcu;
    mcu.advance_cycles(400);
    EXPECT_EQ(mcu.read32(T(0, TC)), 0u) << "disabled at reset";
    mcu.write32(T(0, TCR), 1);
    mcu.advance_cycles(400);  // 100 edges at CCLK/4
    EXPECT_EQ(mcu.read32(T(0, TC)), 100u);
    mcu.write32(T(0, TCR), 3);  // enable + reset: held at 0
    mcu.advance_cycles(400);
    EXPECT_EQ(mcu.read32(T(0, TC)), 0u);
    mcu.write32(T(0, TCR), 1);
    mcu.advance_cycles(3);  // edges fall on multiples of 4 cycles
    EXPECT_EQ(mcu.read32(T(0, TC)), 0u);
    mcu.advance_cycles(1);
    EXPECT_EQ(mcu.read32(T(0, TC)), 1u);
}

TEST(Timer, PrescalerDividesAndPcIsVisible) {
    Lpc1768 mcu;
    mcu.write32(T(1, PR), 2);  // TC every 3 edges
    mcu.write32(T(1, TCR), 1);
    mcu.advance_cycles(4 * 8);  // 8 edges
    EXPECT_EQ(mcu.read32(T(1, TC)), 2u);
    EXPECT_EQ(mcu.read32(T(1, PC)), 2u);
}

TEST(Timer, PclkselChoosesTheDivider) {
    Lpc1768 mcu;
    mcu.write32(PCLKSEL0, 1u << 2);   // TIMER0: CCLK
    mcu.write32(PCLKSEL1, 3u << 12);  // TIMER2: CCLK/8
    EXPECT_EQ(mcu.timer_divider(0), 1u);
    EXPECT_EQ(mcu.timer_divider(1), 4u);
    EXPECT_EQ(mcu.timer_divider(2), 8u);
    mcu.write32(T(0, TCR), 1);
    mcu.write32(T(2, TCR), 1);
    mcu.advance_cycles(80);
    EXPECT_EQ(mcu.read32(T(0, TC)), 80u);
    EXPECT_EQ(mcu.read32(T(2, TC)), 10u);
}

// Figure 114: PR = 2, MR = 6, interrupt and reset: TC holds 6 for a full cycle,
// then 0; the interrupt comes one edge after TC became 6. Period (MR+1)(PR+1).
TEST(Timer, InterruptAndResetOnMatch) {
    Lpc1768 mcu;
    mcu.write32(T(0, PR), 2);
    mcu.write32(MR(0, 0), 6);
    mcu.write32(T(0, MCR), 3);  // MR0I | MR0R
    mcu.write32(T(0, TCR), 1);
    const std::uint64_t edge = 4;
    mcu.advance_cycles((6 * 3) * edge);  // TC just became 6
    EXPECT_EQ(mcu.read32(T(0, TC)), 6u);
    EXPECT_EQ(mcu.read32(T(0, IR)), 0u);
    mcu.advance_cycles(edge);
    EXPECT_EQ(mcu.read32(T(0, IR)), 1u) << "one edge later";
    mcu.advance_cycles(2 * edge);
    EXPECT_EQ(mcu.read32(T(0, TC)), 0u) << "reset at the end of the match cycle";
    mcu.write32(T(0, IR), 1);
    EXPECT_EQ(mcu.read32(T(0, IR)), 0u);
    mcu.advance_cycles(21 * edge);  // one full period later: flagged again
    EXPECT_EQ(mcu.read32(T(0, IR)), 1u);
}

// Figure 115: stop on match clears TCR[0] one edge after TC reaches MR; TC holds.
TEST(Timer, StopOnMatch) {
    Lpc1768 mcu;
    mcu.write32(MR(3, 0), 5);
    mcu.write32(T(3, MCR), 5);  // MR0I | MR0S
    mcu.write32(T(3, TCR), 1);
    mcu.advance_cycles(4 * 100);
    EXPECT_EQ(mcu.read32(T(3, TC)), 5u);
    EXPECT_EQ(mcu.read32(T(3, TCR)), 0u);
    EXPECT_EQ(mcu.read32(T(3, IR)), 1u);
}

TEST(Timer, SeveralChannelsFlagIndependently) {
    Lpc1768 mcu;
    mcu.write32(MR(1, 1), 10);
    mcu.write32(MR(1, 3), 20);
    mcu.write32(MR(1, 2), 30);
    mcu.write32(T(1, MCR), (1u << 3) | (1u << 9) | (3u << 6));  // MR1I, MR3I, MR2I+MR2R
    mcu.write32(T(1, TCR), 1);
    mcu.advance_cycles(4 * 11);
    EXPECT_EQ(mcu.read32(T(1, IR)), 0x2u);
    mcu.advance_cycles(4 * 10);
    EXPECT_EQ(mcu.read32(T(1, IR)), 0xAu);
    mcu.advance_cycles(4 * 10);
    EXPECT_EQ(mcu.read32(T(1, IR)), 0xEu);
    mcu.write32(T(1, IR), 0x8);
    EXPECT_EQ(mcu.read32(T(1, IR)), 0x6u) << "write 1 clears only that bit";
}

TEST(Timer, InterruptReachesTheHandlerThroughTheNvic) {
    latasim::mcb1700::Board board;
    auto& mcu = board.mcu();
    std::vector<std::uint64_t> at;
    mcu.bind_handler(kTimer0Irq, [&] {
        at.push_back(mcu.cycles());
        mcu.write32(T(0, IR), 1);  // clearing the flag drops the line
    });
    mcu.write32(ISER0, 1u << kTimer0Irq);
    mcu.write32(MR(0, 0), 25'000 - 1);  // 1 ms at 25 MHz
    mcu.write32(T(0, MCR), 3);
    mcu.write32(T(0, TCR), 1);
    mcu.advance_cycles(3 * kCyclesPerMillisecond);
    EXPECT_EQ(at, (std::vector<std::uint64_t>{100'000, 200'000, 300'000}));
    const auto& events = mcu.trace().events();
    ASSERT_GE(events.size(), 5u);
    std::vector<std::string> last;  // without "#seq  "
    for (auto it = events.end() - 5; it != events.end(); ++it) last.push_back(to_string(*it).substr(6));
    EXPECT_EQ(last, (std::vector<std::string>{
                        "t=300000     match   TIMER0    MR0",
                        "t=300000     irq     TIMER0    pend",
                        "t=300000     irq     TIMER0    enter",
                        "t=300000     write32 T0IR      0x00000001",
                        "t=300000     irq     TIMER0    exit",
                    }));
}

TEST(Timer, UnclearedFlagKeepsTheIrqPending) {
    Lpc1768 mcu;
    int calls = 0;
    mcu.bind_handler(kTimer0Irq, [&] {
        if (++calls == 3) mcu.write32(T(0, IR), 1);  // cleared only on the third entry
    });
    mcu.write32(ISER0, 1u << kTimer0Irq);
    mcu.write32(MR(0, 0), 10);
    mcu.write32(T(0, MCR), 1);
    mcu.write32(T(0, TCR), 1);
    mcu.advance_cycles(4 * 12);
    EXPECT_EQ(calls, 3) << "level line: re-entered while IR stays set (ARM 4.2.9)";
}

TEST(Timer, LargeAdvancesAreExactAndCheap) {
    Lpc1768 mcu;
    mcu.write32(MR(2, 0), 1'000'000 - 1);
    mcu.write32(T(2, MCR), 2);  // reset only: no interrupt events
    mcu.write32(T(2, PR), 24);
    mcu.write32(T(2, TCR), 1);
    mcu.advance_cycles(3'600ull * 1'000 * kCyclesPerMillisecond + 400);  // an hour and 400 cycles
    // 25 MHz / 25 = 1 MHz TC; an hour is 3600 periods exactly, then 400 cycles = 100 edges.
    EXPECT_EQ(mcu.read32(T(2, TC)), 4u);
    EXPECT_EQ(mcu.read32(T(2, PC)), 0u);
}

TEST(Timer, SimultaneousTimersAndSysTickByPriority) {
    Lpc1768 mcu;
    std::vector<std::string> order;
    for (int irq : {kSysTickIrq, kTimer0Irq, kTimer0Irq + 1})
        mcu.bind_handler(irq, [&mcu, &order, irq] {
            order.push_back(latasim::lpc17xx::irq_name(irq));
            if (irq >= 0) mcu.write32(T(static_cast<unsigned>(irq - kTimer0Irq), IR), 0x3F);
        });
    mcu.write32(ISER0, 3u << kTimer0Irq);
    mcu.write8(0xE000E400 + kTimer0Irq, 4 << 3);  // TIMER0 priority 4
    mcu.write8(0xE000E400 + kTimer0Irq + 1, 1 << 3);  // TIMER1 priority 1
    mcu.write8(0xE000ED23, 2 << 3);  // SysTick priority 2
    for (unsigned n : {0u, 1u}) {  // both flag one edge after TC reaches 99: t = 400
        mcu.write32(MR(n, 0), 99);
        mcu.write32(T(n, MCR), 1);
        mcu.write32(T(n, TCR), 1);
    }
    mcu.write32(0xE000E014, 400);  // SysTick reaches 0 at t = 400 too
    mcu.write32(0xE000E010, 7);
    mcu.advance_cycles(400);
    EXPECT_EQ(order, (std::vector<std::string>{"TIMER1", "SysTick", "TIMER0"}));
}

TEST(Timer, AddressesAndIrqsOfAllFour) {
    EXPECT_EQ(kTimerBase[0], 0x40004000u);
    EXPECT_EQ(kTimerBase[1], 0x40008000u);
    EXPECT_EQ(kTimerBase[2], 0x40090000u);
    EXPECT_EQ(kTimerBase[3], 0x40094000u);
    for (unsigned n = 0; n < 4; ++n) {
        Lpc1768 mcu;
        mcu.write32(MR(n, 0), 0);
        mcu.write32(T(n, MCR), 1);
        mcu.write32(T(n, TC), 0xFFFFFFFF);  // wraps to 0 = MR0 on the next increment
        mcu.write32(T(n, TCR), 1);
        mcu.advance_cycles(8);
        EXPECT_TRUE(mcu.nvic().pending(kTimer0Irq + static_cast<int>(n))) << "TIMER" << n;
        for (unsigned other = 0; other < 4; ++other)
            if (other != n) EXPECT_FALSE(mcu.nvic().pending(kTimer0Irq + static_cast<int>(other)));
    }
}

TEST(Timer, UnmodelledRegistersFault) {
    Lpc1768 mcu;
    EXPECT_THROW(mcu.read32(T(0, 0x28)), BusFault) << "CCR";
    EXPECT_THROW(mcu.write32(T(0, 0x3C), 0), BusFault) << "EMR";
    EXPECT_THROW(mcu.write32(T(0, 0x70), 0), BusFault) << "CTCR";
    EXPECT_THROW(mcu.read16(T(0, TC)), BusFault);
    EXPECT_THROW(mcu.read8(PCLKSEL0), BusFault);
}

// The closed form agrees with the edge-by-edge reference for many configurations
// and ways of splitting time.
TEST(Timer, ClosedFormMatchesEdgeByEdgeReference) {
    const std::uint32_t mcrs[] = {0x0, 0x1, 0x3, 0x5, 0x2, 0x18, 0x49, 0x207, 0x1B, 0x924};
    for (std::uint32_t pr : {0u, 1u, 4u})
        for (std::uint32_t mcr : mcrs)
            for (std::uint64_t step : {1u, 3u, 17u, 100u}) {
                Timer timer;
                Reference ref;
                const std::array<std::uint32_t, 4> mrs = {5, 2, 9, 5};
                timer.write(TimerReg::Pr, pr);
                ref.pr = pr;
                timer.write(TimerReg::Mcr, mcr);
                ref.mcr = mcr;
                for (unsigned n = 0; n < 4; ++n) {
                    timer.write(static_cast<TimerReg>(0x18 + 4 * n), mrs[n]);
                    ref.mr[n] = mrs[n];
                }
                timer.write(TimerReg::Tcr, 1);
                ref.tcr = 1;
                for (int i = 0; i < 40; ++i) {
                    timer.advance(step);
                    for (std::uint64_t e = 0; e < step; ++e) ref.edge();
                    const std::string at = "pr " + std::to_string(pr) + " mcr " + std::to_string(mcr) + " step " +
                                           std::to_string(step) + " i " + std::to_string(i);
                    ASSERT_EQ(timer.read(TimerReg::Tc), ref.tc) << at;
                    ASSERT_EQ(timer.read(TimerReg::Pc), ref.pc) << at;
                    ASSERT_EQ(timer.read(TimerReg::Ir), ref.ir) << at;
                    ASSERT_EQ(timer.read(TimerReg::Tcr), ref.tcr) << at;
                    if (i % 7 == 3) {  // firmware clears flags now and then
                        timer.write(TimerReg::Ir, 0x3F);
                        ref.ir = 0;
                    }
                }
            }
}

TEST(Timer, RepeatedRunsAreIdentical) {
    auto run = [] {
        latasim::mcb1700::Board board;
        auto& mcu = board.mcu();
        mcu.bind_handler(kTimer0Irq, [&] { mcu.write32(T(0, IR), 0x3F); });
        mcu.write32(ISER0, 1u << kTimer0Irq);
        mcu.write32(MR(0, 0), 777);
        mcu.write32(MR(0, 1), 300);
        mcu.write32(T(0, MCR), 3 | (1u << 3));
        mcu.write32(T(0, TCR), 1);
        mcu.advance_cycles(123'457);
        return mcu.trace().events();
    };
    EXPECT_EQ(run(), run());
}
