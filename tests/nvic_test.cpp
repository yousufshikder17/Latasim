// The NVIC and interrupt dispatch (lpc17xx/nvic.hpp, Lpc1768::bind_handler).
#include "LPC17xx.h"

#include "boards/mcb1700/board.hpp"
#include "host/binding.hpp"
#include "lpc17xx/lpc1768.hpp"
#include "lpc17xx/nvic.hpp"
#include "trace/trace.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using latasim::TraceKind;
using latasim::lpc17xx::BusFault;
using latasim::lpc17xx::kAdcIrq;
using latasim::lpc17xx::kEint0Irq;
using latasim::lpc17xx::kSysTickIrq;
using latasim::lpc17xx::kTimer0Irq;
using latasim::lpc17xx::kTimer3Irq;
using latasim::lpc17xx::Lpc1768;
using latasim::lpc17xx::Nvic;

namespace {

constexpr std::uint32_t ISER0 = 0xE000E100;
constexpr std::uint32_t ISER1 = 0xE000E104;
constexpr std::uint32_t ICER0 = 0xE000E180;
constexpr std::uint32_t ISPR0 = 0xE000E200;
constexpr std::uint32_t ICPR0 = 0xE000E280;
constexpr std::uint32_t IABR0 = 0xE000E300;
constexpr std::uint32_t IPR0 = 0xE000E400;
constexpr std::uint32_t SHPR3 = 0xE000ED20;

std::uint32_t bit(int irq) { return 1u << irq; }
void set_priority(Lpc1768& mcu, int irq, std::uint8_t priority) {  // NVIC->IP[irq] = priority << 3
    mcu.write8(IPR0 + static_cast<std::uint32_t>(irq), static_cast<std::uint8_t>(priority << 3));
}

// Records the order handlers run in, and the virtual time of each.
struct Log {
    std::vector<std::string> calls;
    void bind(Lpc1768& mcu, int irq) {
        mcu.bind_handler(irq, [this, irq] { calls.push_back(latasim::lpc17xx::irq_name(irq)); });
    }
};

}  // namespace

TEST(Nvic, ResetState) {
    Lpc1768 mcu;
    EXPECT_EQ(mcu.read32(ISER0), 0u);
    EXPECT_EQ(mcu.read32(ISER1), 0u);
    EXPECT_EQ(mcu.read32(ISPR0), 0u);
    EXPECT_EQ(mcu.read32(IABR0), 0u);
    for (std::uint32_t a = IPR0; a < IPR0 + 36; a += 4) EXPECT_EQ(mcu.read32(a), 0u);
    EXPECT_EQ(mcu.read32(SHPR3), 0u);
}

TEST(Nvic, EnableAndDisableAreSetAndClearBitmaps) {
    Lpc1768 mcu;
    mcu.write32(ISER0, bit(kTimer0Irq) | bit(kAdcIrq));
    mcu.write32(ISER0, bit(kEint0Irq));  // writing 0s changes nothing
    EXPECT_EQ(mcu.read32(ISER0), bit(kTimer0Irq) | bit(kAdcIrq) | bit(kEint0Irq));
    mcu.write32(ICER0, bit(kAdcIrq));
    EXPECT_EQ(mcu.read32(ISER0), bit(kTimer0Irq) | bit(kEint0Irq));
    EXPECT_EQ(mcu.read32(ICER0), mcu.read32(ISER0)) << "ICER reads the enables too";
    mcu.write32(ISER1, 0xFFFFFFFF);
    EXPECT_EQ(mcu.read32(ISER1), 0x7u) << "IRQ32-34 only";
}

TEST(Nvic, PendingSetAndClear) {
    Lpc1768 mcu;
    mcu.write32(ISPR0, bit(kAdcIrq));
    EXPECT_TRUE(mcu.nvic().pending(kAdcIrq));
    EXPECT_EQ(mcu.read32(ICPR0), bit(kAdcIrq));
    mcu.write32(ICPR0, bit(kAdcIrq));
    EXPECT_FALSE(mcu.nvic().pending(kAdcIrq));
}

TEST(Nvic, PrioritiesKeepTheTopFiveBits) {
    Lpc1768 mcu;
    mcu.write8(IPR0 + kAdcIrq, 0xFF);
    EXPECT_EQ(mcu.nvic().priority(kAdcIrq), 0xF8u);
    mcu.write32(IPR0 + 20, 0x12345678);  // IPR5: IRQ20-23
    EXPECT_EQ(mcu.read32(IPR0 + 20), 0x10305078u & 0xF8F8F8F8u);
    mcu.write8(SHPR3 + 3, 0x7F);
    EXPECT_EQ(mcu.nvic().priority(kSysTickIrq), 0x78u);
}

TEST(Nvic, WordOnlyRegistersAndUnmappedAddressesFault) {
    Lpc1768 mcu;
    EXPECT_THROW(mcu.write8(ISER0, 1), BusFault);
    EXPECT_THROW(mcu.read16(ISPR0), BusFault);
    EXPECT_THROW(mcu.write32(IABR0, 1), BusFault) << "IABR is read-only";
    EXPECT_THROW(mcu.read32(ISER0 + 8), BusFault) << "ISER2: no IRQs above 34";
    EXPECT_THROW(mcu.write16(IPR0 + 1, 0), BusFault) << "misaligned";
    EXPECT_THROW(mcu.read32(0xE000ED04), BusFault) << "ICSR is not modelled";
}

TEST(Nvic, PendingEnabledIrqWithAHandlerIsTakenAtOnce) {
    Lpc1768 mcu;
    Log log;
    log.bind(mcu, kAdcIrq);
    mcu.write32(ISER0, bit(kAdcIrq));
    mcu.write32(ISPR0, bit(kAdcIrq));
    EXPECT_EQ(log.calls, (std::vector<std::string>{"ADC"})) << "at the store that pended it";
    EXPECT_FALSE(mcu.nvic().pending(kAdcIrq));
    EXPECT_FALSE(mcu.nvic().active(kAdcIrq));
}

TEST(Nvic, DisabledPendingIrqWaitsUntilEnabled) {
    Lpc1768 mcu;
    Log log;
    log.bind(mcu, kAdcIrq);
    mcu.write32(ISPR0, bit(kAdcIrq));
    mcu.advance_cycles(1'000);
    EXPECT_TRUE(log.calls.empty());
    EXPECT_TRUE(mcu.nvic().pending(kAdcIrq));
    mcu.write32(ISER0, bit(kAdcIrq));  // enabling an already-pending IRQ takes it
    EXPECT_EQ(log.calls.size(), 1u);
}

TEST(Nvic, IrqWithoutAHandlerStaysPending) {
    Lpc1768 mcu;
    mcu.write32(ISER0, bit(kAdcIrq));
    mcu.write32(ISPR0, bit(kAdcIrq));
    EXPECT_TRUE(mcu.nvic().pending(kAdcIrq));
    Log log;
    log.bind(mcu, kAdcIrq);  // binding takes it
    EXPECT_EQ(log.calls.size(), 1u);
}

// Lower priority value first; equal priorities by exception number, so SysTick
// (exception 15) before IRQ0 (16) before IRQ1 ... (ARM DUI 0552A 2.3.5).
TEST(Nvic, SimultaneousPendingIrqsAreTakenByPriorityThenNumber) {
    Lpc1768 mcu;
    Log log;
    for (int irq : {kSysTickIrq, kTimer0Irq, kTimer3Irq, kEint0Irq, kAdcIrq}) log.bind(mcu, irq);
    set_priority(mcu, kTimer3Irq, 2);
    set_priority(mcu, kEint0Irq, 1);
    set_priority(mcu, kAdcIrq, 1);
    mcu.write8(SHPR3 + 3, 2 << 3);  // SysTick priority 2
    mcu.write32(0xE000E014, 9);     // STRELOAD
    mcu.write32(0xE000E010, 0x7);   // STCTRL: SysTick at t = 9, pending with the rest
    // Pend everything while disabled, then enable all at once.
    mcu.write32(ISPR0, bit(kTimer0Irq) | bit(kTimer3Irq) | bit(kEint0Irq) | bit(kAdcIrq));
    mcu.advance_cycles(9);  // SysTick pends and is taken alone
    EXPECT_EQ(log.calls, (std::vector<std::string>{"SysTick"}));
    log.calls.clear();
    mcu.write32(0xE000E010, 0x5);  // TICKINT off
    mcu.write32(ISER0, bit(kTimer0Irq) | bit(kTimer3Irq) | bit(kEint0Irq) | bit(kAdcIrq));
    EXPECT_EQ(log.calls, (std::vector<std::string>{"TIMER0", "EINT0", "ADC", "TIMER3"}));
}

TEST(Nvic, SysTickAndIrqsArbitrateTogether) {
    Lpc1768 mcu;
    Log log;
    log.bind(mcu, kSysTickIrq);
    log.bind(mcu, kAdcIrq);
    mcu.write32(ISER0, bit(kAdcIrq));
    set_priority(mcu, kAdcIrq, 3);
    mcu.write8(SHPR3 + 3, 3 << 3);  // equal priority: SysTick is the lower exception number
    mcu.bind_handler(kSysTickIrq, [&] {
        log.calls.push_back("SysTick");
        mcu.write32(ISPR0, bit(kAdcIrq));  // pended during the handler: taken after it
    });
    mcu.write32(0xE000E014, 99);
    mcu.write32(0xE000E010, 0x7);
    mcu.advance_cycles(99);
    EXPECT_EQ(log.calls, (std::vector<std::string>{"SysTick", "ADC"}));
}

TEST(Nvic, ExceptionPendedInAHandlerIsTakenAfterItReturns) {
    Lpc1768 mcu;
    std::vector<std::string> seen;
    mcu.write32(ISER0, bit(kTimer0Irq) | bit(kAdcIrq));
    set_priority(mcu, kTimer0Irq, 5);
    set_priority(mcu, kAdcIrq, 0);  // higher priority, yet no preemption: not nested
    mcu.bind_handler(kAdcIrq, [&] {
        seen.push_back("ADC active=" + std::to_string(mcu.nvic().active(kTimer0Irq)));
    });
    mcu.bind_handler(kTimer0Irq, [&] {
        seen.push_back("TIMER0 enter");
        mcu.write32(ISPR0, bit(kAdcIrq));
        seen.push_back(std::string("TIMER0 exit, ADC pending=") + (mcu.nvic().pending(kAdcIrq) ? "1" : "0"));
    });
    mcu.write32(ISPR0, bit(kTimer0Irq));
    EXPECT_EQ(seen, (std::vector<std::string>{"TIMER0 enter", "TIMER0 exit, ADC pending=1", "ADC active=0"}));
}

TEST(Nvic, ActiveIsVisibleDuringTheHandler) {
    Lpc1768 mcu;
    std::uint32_t iabr = 0;
    mcu.bind_handler(kAdcIrq, [&] { iabr = mcu.read32(IABR0); });
    mcu.write32(ISER0, bit(kAdcIrq));
    mcu.write32(ISPR0, bit(kAdcIrq));
    EXPECT_EQ(iabr, bit(kAdcIrq));
    EXPECT_EQ(mcu.read32(IABR0), 0u);
}

// ARM DUI 0552A 4.2.9: a level-sensitive signal pends its IRQ while not active,
// pends it again if still asserted when the handler returns, and clearing the
// pending state has no lasting effect while it is asserted.
TEST(Nvic, LevelLinesPendAndRepend) {
    Nvic nvic;
    nvic.set_line(kAdcIrq, true);
    EXPECT_TRUE(nvic.pending(kAdcIrq));
    nvic.write(ICPR0, 4, bit(kAdcIrq));
    EXPECT_TRUE(nvic.pending(kAdcIrq)) << "still asserted";
    nvic.enter(kAdcIrq);
    EXPECT_FALSE(nvic.pending(kAdcIrq));
    nvic.set_line(kAdcIrq, true);
    EXPECT_FALSE(nvic.pending(kAdcIrq)) << "not while active";
    nvic.exit(kAdcIrq);
    EXPECT_TRUE(nvic.pending(kAdcIrq)) << "asserted at return: pending again";
    nvic.enter(kAdcIrq);
    nvic.set_line(kAdcIrq, false);
    nvic.exit(kAdcIrq);
    EXPECT_FALSE(nvic.pending(kAdcIrq));
}

TEST(Nvic, DispatchIsTracedWithIrqAndTime) {
    latasim::mcb1700::Board board;
    auto& mcu = board.mcu();
    mcu.bind_handler(kAdcIrq, [] {});
    mcu.advance_cycles(500);
    mcu.write32(ISER0, bit(kAdcIrq));
    mcu.write32(ISPR0, bit(kAdcIrq));
    const auto& events = mcu.trace().events();
    ASSERT_EQ(events.size(), 5u);
    EXPECT_EQ(to_string(events[1]), "#2    t=500        write32 ISPR0     0x00400000");
    EXPECT_EQ(to_string(events[2]), "#3    t=500        irq     ADC       pend");
    EXPECT_EQ(to_string(events[3]), "#4    t=500        irq     ADC       enter");
    EXPECT_EQ(to_string(events[4]), "#5    t=500        irq     ADC       exit");
    EXPECT_EQ(events[3].kind, TraceKind::Interrupt);
    EXPECT_EQ(events[3].irq, kAdcIrq);
}

TEST(Nvic, ThreadModeStepRunsAfterExceptionsReturn) {
    Lpc1768 mcu;
    std::vector<std::string> seen;
    mcu.on_thread_mode([&] { seen.push_back("main"); });
    mcu.bind_handler(kAdcIrq, [&] { seen.push_back("ADC"); });
    mcu.bind_handler(kTimer0Irq, [&] { seen.push_back("TIMER0"); });
    mcu.write32(ISPR0, bit(kAdcIrq) | bit(kTimer0Irq));
    EXPECT_TRUE(seen.empty()) << "nothing taken while disabled";
    mcu.write32(ISER0, bit(kAdcIrq) | bit(kTimer0Irq));
    EXPECT_EQ(seen, (std::vector<std::string>{"TIMER0", "ADC", "main"})) << "once, after both";
}

TEST(Nvic, BadIrqNumbersAreRejected) {
    Lpc1768 mcu;
    EXPECT_THROW(mcu.bind_handler(-2, [] {}), std::out_of_range) << "PendSV and the rest are not delivered";
    EXPECT_THROW(mcu.bind_handler(35, [] {}), std::out_of_range);
}

// The host CMSIS functions (host/cmsis_system.cpp) go through the same registers.
TEST(Nvic, CmsisFunctionsUseTheModelledRegisters) {
    latasim::mcb1700::Board board;
    latasim::host::FirmwareBinding bind(board);
    auto& mcu = board.mcu();
    NVIC_EnableIRQ(ADC_IRQn);
    EXPECT_EQ(mcu.peek32(ISER0), bit(kAdcIrq));
    NVIC_SetPriority(ADC_IRQn, 7);
    EXPECT_EQ(mcu.nvic().priority(kAdcIrq), 7u << 3);
    EXPECT_EQ(NVIC_GetPriority(ADC_IRQn), 7u);
    NVIC_SetPriority(SysTick_IRQn, 31);
    EXPECT_EQ(NVIC_GetPriority(SysTick_IRQn), 31u);
    NVIC_DisableIRQ(ADC_IRQn);
    NVIC_SetPendingIRQ(ADC_IRQn);
    EXPECT_EQ(NVIC_GetPendingIRQ(ADC_IRQn), 1u);
    NVIC_ClearPendingIRQ(ADC_IRQn);
    EXPECT_EQ(NVIC_GetPendingIRQ(ADC_IRQn), 0u);
    NVIC_EnableIRQ(SysTick_IRQn);  // ignored for core exceptions, as in CMSIS
    EXPECT_EQ(mcu.peek32(ISER0), 0u);
    EXPECT_EQ(NVIC_GetActive(ADC_IRQn), 0u);
}

TEST(Nvic, RepeatedRunsAreIdentical) {
    auto run = [] {
        latasim::mcb1700::Board board;
        auto& mcu = board.mcu();
        for (int irq : {kSysTickIrq, kTimer0Irq, kAdcIrq}) mcu.bind_handler(irq, [] {});
        mcu.write32(ISER0, bit(kTimer0Irq) | bit(kAdcIrq));
        mcu.write32(0xE000E014, 999);
        mcu.write32(0xE000E010, 0x7);
        for (int i = 0; i < 20; ++i) {
            mcu.advance_cycles(137);
            mcu.write32(ISPR0, bit(i % 2 ? kTimer0Irq : kAdcIrq));
        }
        return mcu.trace().events();
    };
    EXPECT_EQ(run(), run());
}
