// The external-firmware path (docs/external-firmware.md): a C firmware folder
// built by latasim_add_host_firmware and run as a bare-metal workbench scenario.
// The repo-owned sample (external_firmware/sample_firmware.c) stands in for
// external firmware; the bare-metal main() itself is tested in workbench_test.cpp.
#include "boards/mcb1700/board.hpp"
#include "trace/trace.hpp"
#include "workbench/session.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

extern "C" int latasim_sample_main(void);
extern volatile unsigned long sample_passes;

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
    Session s(bare([] { latasim_sample_main(); }));
    ASSERT_FALSE(s.faulted()) << s.fault();
    EXPECT_EQ(s.now(), 0u) << "set-up before the first delay takes no time";
    const auto& glcd = s.board().glcd();
    EXPECT_EQ(glcd.pixel(8, 16), 0xF800u);
    EXPECT_EQ(glcd.pixel(11, 17), 0xF800u);
    EXPECT_EQ(glcd.pixel(12, 16), 0u);
    EXPECT_EQ(glcd.pixel(8, 18), 0u);
    EXPECT_EQ(s.board().led(0), LedState::Off);

    // delay(1000) is 1000 __NOP()s of 10 cycles: the stores after it land exactly.
    s.run_until(9'999);
    EXPECT_EQ(s.board().led(0), LedState::Off);
    s.run_until(10'000);
    EXPECT_EQ(s.board().led(0), LedState::On) << "computed bit-band alias through a pointer";
    s.run_until(19'999);
    EXPECT_EQ(s.board().led(0), LedState::On);
    s.run_until(20'000);
    EXPECT_EQ(s.board().led(0), LedState::Off) << "literal bit-band alias";

    std::vector<std::uint64_t> alias_writes;
    for (const auto& e : events_of(s, TraceKind::Write))
        if (e.address == 0x233806F0) alias_writes.push_back(e.cycles);
    EXPECT_EQ(alias_writes, (std::vector<std::uint64_t>{10'000, 20'000}));
    const auto leds = events_of(s, TraceKind::Led);
    ASSERT_GE(leds.size(), 3u);  // driven off, on, off
    EXPECT_EQ(leds[1].cycles, 10'000u);
    EXPECT_EQ(leds[2].cycles, 20'000u);

    // The idle loop consumes time, so running on neither hangs nor faults.
    const unsigned long passes = sample_passes;
    s.run_for(100 * latasim::lpc17xx::kCyclesPerMillisecond);
    EXPECT_FALSE(s.faulted()) << s.fault();
    EXPECT_EQ(s.now(), 10'020'000u);
    EXPECT_GT(sample_passes, passes);
}
