// The external-firmware path (docs/external-firmware.md): a C firmware folder
// built by latasim_add_host_firmware and run as a bare-metal workbench scenario.
// The repo-owned sample (external_firmware/sample_firmware.c) stands in for
// external firmware; the bare-metal main() itself is tested in workbench_test.cpp.
#include "boards/mcb1700/board.hpp"
#include "trace/trace.hpp"
#include "workbench/session.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" int latasim_sample_main(void);
namespace latasim_sample_main_firmware {  // each firmware's globals are in its namespace
extern volatile unsigned long sample_passes;
}
using latasim_sample_main_firmware::sample_passes;
std::vector<int> latasim_sample_main_bind_handlers(latasim::lpc17xx::Lpc1768& mcu);

extern "C" int latasim_irq_sample_main(void);
namespace latasim_irq_sample_main_firmware {
extern volatile unsigned long irq_sample_ticks;
extern volatile unsigned long irq_sample_matches;
}
using latasim_irq_sample_main_firmware::irq_sample_matches;
using latasim_irq_sample_main_firmware::irq_sample_ticks;
std::vector<int> latasim_irq_sample_main_bind_handlers(latasim::lpc17xx::Lpc1768& mcu);

extern "C" int latasim_adc_sample_main(void);
namespace latasim_adc_sample_main_firmware {
extern volatile long adc_sample_last;
}
using latasim_adc_sample_main_firmware::adc_sample_last;
std::vector<int> latasim_adc_sample_main_bind_handlers(latasim::lpc17xx::Lpc1768& mcu);

extern "C" void SysTick_Handler(void);  // Keil's Blinky_ULp IRQ.c, a built-in scenario's

using latasim::TraceEvent;
using latasim::TraceKind;
using latasim::mcb1700::LedState;
using latasim::workbench::Scenario;
using latasim::workbench::Session;

namespace {

Scenario bare(std::function<void()> main) {
    Scenario s;
    s.name = "bare-metal test";
    s.bare_main = std::move(main);
    return s;
}

std::vector<TraceEvent> events_of(const Session& s, TraceKind kind) {
    std::vector<TraceEvent> out;
    for (const auto& e : s.board().mcu().trace().events())
        if (e.kind == kind) out.push_back(e);
    return out;
}

}  // namespace

TEST(ExternalFirmware, SampleDrawsOverSsp1AndBitBandsInVirtualTime) {
    // The sample sends 62 SSP1 frames (seven register writes of two 3-byte words,
    // the GRAM index word, the data start byte, eight 2-byte pixels), each 8 bits at
    // PCLK / (CPSDVSR 2 * (SCR 1 + 1)) with PCLK = CCLK / 4: 128 cycles a frame.
    constexpr std::uint64_t kDrawn = 62 * 128;  // 7,936
    Session s(bare([] { latasim_sample_main(); }));
    ASSERT_FALSE(s.faulted()) << s.fault();
    EXPECT_EQ(s.now(), 0u);
    s.run_until(kDrawn);
    const auto& glcd = s.board().glcd();
    EXPECT_EQ(glcd.pixel(8, 16), 0xF800u);
    EXPECT_EQ(glcd.pixel(11, 17), 0xF800u);
    EXPECT_EQ(glcd.pixel(12, 16), 0u);
    EXPECT_EQ(glcd.pixel(8, 18), 0u);
    EXPECT_EQ(s.board().led(0), LedState::Off);

    // Then delay(1000) is 1000 __NOP()s of 10 cycles: the stores after it land exactly.
    s.run_until(kDrawn + 9'999);
    EXPECT_EQ(s.board().led(0), LedState::Off);
    s.run_until(kDrawn + 10'000);
    EXPECT_EQ(s.board().led(0), LedState::On) << "computed bit-band alias through a pointer";
    s.run_until(kDrawn + 19'999);
    EXPECT_EQ(s.board().led(0), LedState::On);
    s.run_until(kDrawn + 20'000);
    EXPECT_EQ(s.board().led(0), LedState::Off) << "literal bit-band alias";

    std::vector<std::uint64_t> alias_writes;
    for (const auto& e : events_of(s, TraceKind::Write))
        if (e.address == 0x233806F0) alias_writes.push_back(e.cycles);
    EXPECT_EQ(alias_writes, (std::vector<std::uint64_t>{kDrawn + 10'000, kDrawn + 20'000}));
    const auto leds = events_of(s, TraceKind::Led);
    ASSERT_GE(leds.size(), 3u);  // driven off, on, off
    EXPECT_EQ(leds[1].cycles, kDrawn + 10'000);
    EXPECT_EQ(leds[2].cycles, kDrawn + 20'000);
    std::uint64_t last_frame = 0;
    for (const auto& e : events_of(s, TraceKind::Write))
        if (e.address == 0x40030008) last_frame = e.cycles;
    EXPECT_EQ(last_frame, kDrawn - 128) << "each frame is sent once the one before it is on the wire";

    // The idle loop consumes time, so running on neither hangs nor faults.
    const unsigned long passes = sample_passes;
    s.run_for(100 * latasim::lpc17xx::kCyclesPerMillisecond);
    EXPECT_FALSE(s.faulted()) << s.fault();
    EXPECT_EQ(s.now(), kDrawn + 10'020'000);
    EXPECT_GT(sample_passes, passes);
}

// Interrupt handlers of external firmware (latasim_add_host_firmware's generated
// <entry>_bind_handlers): only the ones the firmware defines are bound.
namespace {

Scenario with_handlers(std::function<void()> main, std::vector<int> (*bind)(latasim::lpc17xx::Lpc1768&),
                       std::vector<int>& bound) {
    Scenario s = bare(std::move(main));
    s.start = [bind, &bound](Session& session) { bound = bind(session.board().mcu()); };
    return s;
}

std::size_t index_of(const std::string& name) {
    const auto& all = latasim::workbench::scenarios();
    for (std::size_t i = 0; i < all.size(); ++i)
        if (all[i].name == name) return i;
    throw std::logic_error("no scenario " + name);
}

}  // namespace

TEST(ExternalFirmware, SysTickAndPeripheralHandlersRun) {
    std::vector<int> bound;
    const unsigned long ticks = irq_sample_ticks, matches = irq_sample_matches;
    Session s(with_handlers([] { latasim_irq_sample_main(); }, latasim_irq_sample_main_bind_handlers, bound));
    EXPECT_EQ(bound, (std::vector<int>{latasim::lpc17xx::kSysTickIrq, latasim::lpc17xx::kTimer0Irq}))
        << "handlers.c is linked although nothing else references it";
    s.run_for(50 * latasim::lpc17xx::kCyclesPerMillisecond);
    ASSERT_FALSE(s.faulted()) << s.fault();
    EXPECT_EQ(irq_sample_ticks - ticks, 5u) << "10 ms SysTick";
    EXPECT_EQ(irq_sample_matches - matches, 50u) << "1 ms TIMER0 match";
    EXPECT_EQ(s.board().led(0), (irq_sample_ticks & 1) ? LedState::On : LedState::Off);
    std::size_t enters = 0;
    for (const auto& e : events_of(s, TraceKind::Interrupt)) enters += e.value == latasim::kInterruptEnter;
    EXPECT_EQ(enters, 55u);
}

TEST(ExternalFirmware, AbsentHandlersAreNotBound) {
    std::vector<int> bound{99};
    Session s(with_handlers([] { latasim_sample_main(); }, latasim_sample_main_bind_handlers, bound));
    EXPECT_TRUE(bound.empty());
    s.run_for(1'000'000);
    EXPECT_FALSE(s.faulted()) << s.fault();
}

TEST(ExternalFirmware, BoardAdcDriverAndItsHandler) {
    std::vector<int> bound;
    Session s(with_handlers([] { latasim_adc_sample_main(); }, latasim_adc_sample_main_bind_handlers, bound));
    EXPECT_EQ(bound, (std::vector<int>{latasim::lpc17xx::kSysTickIrq, latasim::lpc17xx::kAdcIrq}))
        << "the firmware's SysTick_Handler and the board driver's ADC_IRQHandler";
    s.input([](latasim::mcb1700::Board& b) { b.set_potentiometer(0xABC); });
    s.run_for(25 * latasim::lpc17xx::kCyclesPerMillisecond);
    ASSERT_FALSE(s.faulted()) << s.fault();
    EXPECT_EQ(adc_sample_last, 0xABC);
    EXPECT_EQ(events_of(s, TraceKind::AdcConversion).size(), 2u) << "started at 10 and 20 ms";
}

// A built-in scenario's SysTick_Handler (Keil's IRQ.c) and an external firmware's
// are different functions, and each session binds its own.
TEST(ExternalFirmware, BuiltInHandlersStayIsolated) {
    std::vector<int> bound;
    {
        Session s(with_handlers([] { latasim_irq_sample_main(); }, latasim_irq_sample_main_bind_handlers, bound));
        s.run_for(20 * latasim::lpc17xx::kCyclesPerMillisecond);
    }
    const unsigned long ticks = irq_sample_ticks;
    Session blinky(index_of("Blinky_ULp"));
    blinky.run_until(blinky.scenario().check_after);
    EXPECT_TRUE(blinky.check().passed) << blinky.check().detail;
    EXPECT_EQ(irq_sample_ticks, ticks) << "the external handler did not run for the built-in scenario";
    EXPECT_NE(reinterpret_cast<void*>(&SysTick_Handler), nullptr);
}
