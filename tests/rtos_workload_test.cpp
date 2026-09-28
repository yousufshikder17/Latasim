// Representative CMSIS-RTOS v1 firmware (firmware/rtos/workloads.h), compiled as C
// against RTX 4's cmsis_os.h, running on the RTOS model on a modelled MCB1700.
//
// Ticks: firmware's main calls osKernelInitialize/osKernelStart again after RTX's
// start-up, and that restart reprograms SysTick, so ticks fall on exact multiples of
// the tick period (rtos_kernel_test.cpp).
#include "workloads.h"

#include "cmsis_os.h"

#include "boards/mcb1700/board.hpp"
#include "host/binding.hpp"
#include "host/rtos_binding.hpp"
#include "lpc17xx/rtos_port.hpp"
#include "rtos/kernel.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace latasim;
using mcb1700::LedState;

namespace {

constexpr std::uint64_t kMs = 100'000;
constexpr std::uint32_t at_ms(std::uint32_t ms) { return ms * 100'000u; }  // osKernelSysTick value

struct RtosBoard {
    mcb1700::Board board;
    lpc17xx::RtosPort port{board.mcu()};
    rtos::Kernel kernel;
    host::FirmwareBinding bind{board};
    host::RtosBinding bind_rtos{kernel};
    explicit RtosBoard(rtos::Config c = {}) : kernel(port, c) {}
    void run(void (*firmware_main)(), std::uint64_t cycles) {
        kernel.start_main(firmware_main);
        kernel.run_for(cycles);
    }
};

}  // namespace

// Three threads, 40, 30 and 20 ms of work, 15 ms time slices (5 ms ticks, a 3-tick
// round-robin timeout).
TEST(RtosWorkload, RoundRobinInterleavesFiniteThreads) {
    rtos::Config c;
    c.tick_period = 500'000;
    c.tick_us = 5'000;
    c.round_robin_ticks = 3;
    RtosBoard r(c);
    rr_reset();
    r.run(rr_main, 22 * kMs);
    // A ran 0-15 ms (its 15th step ends on the switch and is counted when it runs
    // again), B from 15 ms.
    EXPECT_EQ(rr_steps[0], 14u);
    EXPECT_EQ(rr_steps[1], 7u);
    EXPECT_EQ(rr_steps[2], 0u);
    r.kernel.run_for(200 * kMs);
    EXPECT_EQ(rr_steps[0], 40u);
    EXPECT_EQ(rr_steps[1], 30u);
    EXPECT_EQ(rr_steps[2], 20u);
    // Slices: A 0-15, B 15-30, C 30-45, A 45-60, B 60-75, C 75-80 (done), A 80-90 (done).
    // B's work ended at 75 ms together with its slice, so it records completion when it
    // next runs: after A, at 90 ms. No idle time until all 90 ms of work is done.
    EXPECT_EQ(rr_done_at[2], at_ms(80));
    EXPECT_EQ(rr_done_at[0], at_ms(90));
    EXPECT_EQ(rr_done_at[1], at_ms(90));
    EXPECT_EQ(r.kernel.idle_cycles(), (222 - 90) * kMs);
}

TEST(RtosWorkload, PreemptiveTasksFinishByPriority) {
    RtosBoard r;
    pre_reset();
    r.run(pre_main, 200 * kMs);
    ASSERT_EQ(pre_finished, 5u);
    EXPECT_EQ((std::vector<std::uint32_t>(pre_finish_order, pre_finish_order + 5)),
              (std::vector<std::uint32_t>{2, 0, 3, 1, 4}))
        << "High, then AboveNormal and Normal each in creation order";
    EXPECT_DOUBLE_EQ(pre_result[0], 66'306.0);
    EXPECT_NEAR(pre_result[3], 1.0 + 5 + 12.5 + 125.0 / 6 + 625.0 / 24 + 3125.0 / 120, 1e-9);
}

TEST(RtosWorkload, YieldAlternatesEqualThreads) {
    rtos::Config c;
    c.round_robin = false;
    c.call_cycles = 250;
    RtosBoard r(c);
    yield_reset();
    r.run(yield_main, kMs);
    EXPECT_EQ(std::string(const_cast<const char*>(yield_order)).substr(0, 8), "ABABABAB");
    EXPECT_LE(yield_count[0] - yield_count[1], 1u);
    EXPECT_GT(yield_count[0], 100u);
}

TEST(RtosWorkload, DelaysSetThreadFrequencies) {
    RtosBoard r;
    delay_reset();
    r.run(delay_main, 1000 * kMs);
    EXPECT_EQ(delay_count[0], 101u) << "t = 0 and each of 100 ticks";
    EXPECT_EQ(delay_count[1], 51u);
    EXPECT_EQ(r.kernel.idle_cycles(), 1000 * kMs);
}

TEST(RtosWorkload, SignalsAndMutexCoordinateFiniteServices) {
    RtosBoard r;
    svc_reset();
    r.run(svc_main, 100 * kMs);
    EXPECT_EQ(std::string(svc_log), "app: start of message, end");
    EXPECT_EQ(std::string(const_cast<const char*>(svc_order)), "CMADU");
    for (unsigned n = 0; n < 5; ++n) EXPECT_EQ(svc_counter[n], 1u) << n;
    for (const auto& t : r.kernel.threads())
        if (t.id >= 3) EXPECT_EQ(t.state, rtos::ThreadState::Terminated) << t.name;
}

TEST(RtosWorkload, VirtualTimersDriveLeds) {
    RtosBoard r;
    vt_reset();
    r.run(vt_main, 250 * kMs);
    EXPECT_EQ(vt_fired[0], 5u);
    EXPECT_EQ(vt_fired[1], 3u);
    EXPECT_EQ(vt_fired[2], 2u);
    EXPECT_EQ(r.board.led(0), LedState::On) << "toggled 5 times";
    EXPECT_EQ(r.board.led(1), LedState::On);
    EXPECT_EQ(r.board.led(2), LedState::Off);
    r.kernel.run_for(750 * kMs);
    EXPECT_EQ(vt_fired[0], 20u);
    EXPECT_EQ(vt_fired[1], 12u);
    EXPECT_EQ(vt_fired[2], 8u);
}

// C (200, 50), B (400, 100), A (400, 150): U = 0.875. Per 400 ms: C 0-50, B 50-150,
// A 150-200, C 200-250 (preempting A), A 250-350, idle 350-400.
TEST(RtosWorkload, RateMonotonicTimeline) {
    RtosBoard r;
    rms_reset();
    r.run(rms_main, 800 * kMs);
    const auto jobs = [](const volatile std::uint32_t (&a)[RMS_JOBS], unsigned n) {
        return std::vector<std::uint32_t>(a, a + n);
    };
    EXPECT_EQ(jobs(rms_release[2], 4), (std::vector<std::uint32_t>{0, at_ms(200), at_ms(400), at_ms(600)}));
    EXPECT_EQ(jobs(rms_complete[2], 4), (std::vector<std::uint32_t>{at_ms(50), at_ms(250), at_ms(450), at_ms(650)}));
    EXPECT_EQ(jobs(rms_complete[1], 2), (std::vector<std::uint32_t>{at_ms(150), at_ms(550)}));
    EXPECT_EQ(jobs(rms_complete[0], 2), (std::vector<std::uint32_t>{at_ms(350), at_ms(750)}));
    EXPECT_EQ(r.kernel.idle_cycles(), 100 * kMs) << "350-400 and 750-800";
}

// Low holds the resource from 0 for 80 ms of work; Medium runs 50-150 ms; High
// wants the resource from 60 ms.
TEST(RtosWorkload, RawPriorityInversionDelaysHighBehindMedium) {
    RtosBoard r;
    inv_reset(0);
    r.run(inv_main, 300 * kMs);
    EXPECT_EQ(inv_medium_done_at, at_ms(150));
    EXPECT_EQ(inv_low_done_at, at_ms(180));
    EXPECT_EQ(inv_high_got_resource_at, at_ms(190)) << "its next poll after Low finished";
}

TEST(RtosWorkload, PriorityElevationResolvesTheInversion) {
    RtosBoard r;
    inv_reset(1);
    r.run(inv_main, 300 * kMs);
    EXPECT_EQ(inv_high_got_resource_at, at_ms(90)) << "Low, raised, finished its remaining 30 ms first";
    EXPECT_EQ(inv_medium_done_at, at_ms(180));
}

TEST(RtosWorkload, MutexPriorityInheritanceResolvesTheInversion) {
    RtosBoard r;
    inv_reset(2);
    r.run(inv_main, 300 * kMs);
    EXPECT_EQ(inv_high_got_resource_at, at_ms(90));
    EXPECT_EQ(inv_medium_done_at, at_ms(180));
    bool inherited = false;
    for (const auto& e : r.board.mcu().trace().events())
        inherited |= e.kind == TraceKind::Rtos && e.op == RtosOp::PriorityInherit;
    EXPECT_TRUE(inherited);
}

TEST(RtosWorkload, UnsupportedCallsStopTheThreadWithAnError) {
    RtosBoard r;
    r.kernel.start_main([] { osSemaphoreCreate(nullptr, 1); });
    EXPECT_THROW(r.kernel.run_for(kMs), std::runtime_error);
}

TEST(RtosWorkload, RepeatedRunsAreIdentical) {
    auto run = [] {
        RtosBoard r;
        inv_reset(2);
        r.run(inv_main, 300 * kMs);
        return std::pair{r.board.mcu().trace().events(), r.kernel.timeline()};
    };
    const auto first = run();
    const auto second = run();
    EXPECT_EQ(first.first, second.first);
    EXPECT_TRUE(first.second == second.second);
}
