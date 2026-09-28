// The RTX behavioural kernel (rtos/kernel.hpp) through its C++ interface, on an
// LPC1768 whose SysTick is the kernel tick. Default configuration: 10 ms ticks of
// 1,000,000 core cycles.
//
// Tick times: RTX's start-up starts the kernel, then firmware's main calls
// osKernelInitialize and osKernelStart again, and that second start programs
// SysTick again (rt_sys_start). Its STCURR write, with SysTick already enabled,
// restarts the count one cycle later (E12), so the ticks fall at exact multiples of
// 1,000,000 cycles.
#include "lpc17xx/lpc1768.hpp"
#include "lpc17xx/rtos_port.hpp"
#include "rtos/kernel.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace latasim;
using rtos::Config;
using rtos::Interval;
using rtos::Kernel;
using rtos::Status;
using rtos::ThreadId;
using rtos::ThreadState;

namespace {

constexpr std::uint64_t kMs = 100'000;  // core cycles per millisecond
constexpr std::uint64_t kTick = 1'000'000;
constexpr std::uint64_t kFirstTick = kTick;

struct Rtos {
    lpc17xx::Lpc1768 mcu;
    lpc17xx::RtosPort port{mcu};
    Kernel k;
    explicit Rtos(Config c = {}) : k(port, c) {}

    // main creates the threads, as firmware does, and returns (osThreadExit).
    void start(std::function<void(Kernel&)> create) {
        k.start_main([this, create] {
            k.initialize();
            create(k);
            k.start();
        });
    }
    std::vector<Interval> timeline_of(ThreadId id) const {
        std::vector<Interval> out;
        for (const auto& i : k.timeline())
            if (i.thread == id) out.push_back(i);
        return out;
    }
    std::vector<std::string> rtos_trace() const {
        std::vector<std::string> out;
        for (const auto& e : mcu.trace().events())
            if (e.kind == TraceKind::Rtos) out.push_back(to_string(e).substr(19));
        return out;
    }
};

Config round_robin(std::uint32_t ticks) {
    Config c;
    c.round_robin_ticks = ticks;
    return c;
}

}  // namespace

TEST(RtosKernel, StartUpRunsTheTimerThreadThenMainThenTheIdleDemon) {
    Rtos r;
    int main_ran = 0;
    r.k.start_main([&] { ++main_ran; });
    EXPECT_EQ(main_ran, 0) << "nothing runs until time is given";
    r.k.run_for(1);
    EXPECT_EQ(main_ran, 1);
    const auto threads = r.k.threads();
    ASSERT_EQ(threads.size(), 3u);
    EXPECT_EQ(threads[0].name, "os_idle_demon");
    EXPECT_EQ(threads[1].name, "osTimerThread");
    EXPECT_EQ(threads[1].state, ThreadState::WaitingMessage);
    EXPECT_EQ(threads[1].level, 6) << "OS_TIMERPRIO 5: osPriorityHigh";
    EXPECT_EQ(threads[2].name, "main");
    EXPECT_EQ(threads[2].state, ThreadState::Terminated) << "returning from main terminates it";
    EXPECT_EQ(r.k.running_thread(), 0u) << "idle demon";
    EXPECT_EQ(r.rtos_trace(), (std::vector<std::string>{
                                  "rtos    T1        create High",
                                  "rtos    T2        create Normal",
                                  "rtos    idle      kernel-start Demon",
                                  "rtos    T1        run High",
                                  "rtos    T2        run Normal",
                                  "rtos    T2        terminate",
                                  "rtos    idle      run Demon",
                              }));
}

TEST(RtosKernel, StartProgramsSysTickLikeRtx) {
    Rtos r;
    r.k.start_main([] {});
    const auto& e = r.mcu.trace().events();
    std::vector<std::string> writes;
    for (const auto& ev : e)
        if (ev.kind == TraceKind::Write) writes.push_back(to_string(ev).substr(19));
    EXPECT_EQ(writes, (std::vector<std::string>{"write32 STRELOAD  0x000F423F", "write32 STCURR    0x00000000",
                                                "write32 STCTRL    0x00000007", "write32 SHPR3     0xFF000000"}));
    EXPECT_EQ(r.mcu.nvic().priority(lpc17xx::kSysTickIrq), 0xF8u);
}

// Two equal-priority endless workers share the processor in time slices.
TEST(RtosKernel, RoundRobinAlternatesEachTimeSlice) {
    Rtos r(round_robin(1));
    int a = 0, b = 0;
    ThreadId ta = 0, tb = 0;
    r.start([&](Kernel& k) {
        ta = k.create_thread([&] { for (;;) { k.consume(kMs); ++a; } }, 0, "A");
        tb = k.create_thread([&] { for (;;) { k.consume(kMs); ++b; } }, 0, "B");
    });
    r.k.run_for(65 * kMs);
    EXPECT_EQ(r.timeline_of(ta), (std::vector<Interval>{{0, kFirstTick, ta},
                                                        {kFirstTick + kTick, kFirstTick + 2 * kTick, ta},
                                                        {kFirstTick + 3 * kTick, kFirstTick + 4 * kTick, ta},
                                                        {kFirstTick + 5 * kTick, 65 * kMs, ta}}));
    EXPECT_EQ(r.timeline_of(tb).front(), (Interval{kFirstTick, kFirstTick + kTick, tb}));
    EXPECT_EQ(r.k.idle_cycles(), 0u);
    // 65 steps of 1 ms completed; B's last one ended on the tick that switched to A,
    // so B counts it when it next runs.
    EXPECT_EQ(a + b, 64);
}

TEST(RtosKernel, RoundRobinTimeoutIsConfigurable) {
    Rtos r(round_robin(3));  // 30 ms slices
    ThreadId ta = 0, tb = 0;
    r.start([&](Kernel& k) {
        ta = k.create_thread([&] { for (;;) k.consume(kMs); }, 0, "A");
        tb = k.create_thread([&] { for (;;) k.consume(kMs); }, 0, "B");
    });
    r.k.run_for(100 * kMs);
    // rt_chk_robin: the slice starts counting at the first tick the thread sees, so
    // the first one is 3 ticks from that tick.
    EXPECT_EQ(r.timeline_of(ta).front(), (Interval{0, kFirstTick + 2 * kTick, ta}));
    EXPECT_EQ(r.timeline_of(tb).front(), (Interval{kFirstTick + 2 * kTick, kFirstTick + 5 * kTick, tb}));
}

TEST(RtosKernel, WithoutRoundRobinTheFirstEqualThreadKeepsRunning) {
    Config c;
    c.round_robin = false;
    Rtos r(c);
    int b = 0;
    ThreadId ta = 0;
    r.start([&](Kernel& k) {
        ta = k.create_thread([&] { for (;;) k.consume(kMs); }, 0, "A");
        k.create_thread([&] { ++b; }, 0, "B");
    });
    r.k.run_for(100 * kMs);
    EXPECT_EQ(b, 0);
    EXPECT_EQ(r.timeline_of(ta), (std::vector<Interval>{{0, 100 * kMs, ta}}));
}

TEST(RtosKernel, HigherPriorityRunsFirstAndStarvesLowerWhileItRuns) {
    Rtos r;
    int low = 0;
    ThreadId th = 0;
    r.start([&](Kernel& k) {
        k.create_thread([&] { ++low; }, 0, "Normal");
        th = k.create_thread([&] { k.consume(25 * kMs); }, 1, "AboveNormal");
    });
    r.k.run_for(20 * kMs);
    EXPECT_EQ(low, 0) << "created first, but lower priority";
    r.k.run_for(10 * kMs);
    EXPECT_EQ(low, 1) << "runs once the higher thread terminates";
    EXPECT_EQ(r.timeline_of(th), (std::vector<Interval>{{0, 25 * kMs, th}}));
}

// A thread readied by a lower one preempts it at once.
TEST(RtosKernel, CreatingAHigherThreadFromARunningThreadPreemptsIt) {
    Rtos r;
    std::vector<std::string> order;
    r.start([&](Kernel& k) {
        k.create_thread(
            [&] {
                order.push_back("low before");
                k.create_thread([&] { order.push_back("high"); }, 2, "H");
                order.push_back("low after");
            },
            0, "L");
    });
    r.k.run_for(1);
    EXPECT_EQ(order, (std::vector<std::string>{"low before", "high", "low after"}));
}

TEST(RtosKernel, YieldHandsOverToTheNextEqualThread) {
    Config c;
    c.round_robin = false;
    c.call_cycles = 250;
    Rtos r(c);
    std::string order;
    ThreadId ta = 0;
    r.start([&](Kernel& k) {
        ta = k.create_thread([&] { for (;;) { order += 'A'; k.yield(); } }, 0, "A");
        k.create_thread([&] { for (;;) { order += 'B'; k.yield(); } }, 0, "B");
    });
    r.k.run_for(10'000);
    ASSERT_GE(order.size(), 30u);
    for (std::size_t i = 0; i < order.size(); ++i) EXPECT_EQ(order[i], i % 2 ? 'B' : 'A') << i;
    // Each turn is one kernel call: 250 cycles, then the other thread.
    const auto turns = r.timeline_of(ta);
    for (std::size_t i = 0; i + 1 < turns.size(); ++i) EXPECT_EQ(turns[i].end - turns[i].start, 250u);
}

TEST(RtosKernel, YieldWithoutAnEqualThreadKeepsRunning) {
    Config c;
    c.call_cycles = 100;
    Rtos r(c);
    int n = 0, lower = 0;
    r.start([&](Kernel& k) {
        k.create_thread([&] { for (;;) { ++n; k.yield(); } }, 1, "Alone");
        k.create_thread([&] { ++lower; }, 0, "Lower");
    });
    r.k.run_for(1'000);
    EXPECT_EQ(lower, 0);
    // The timer thread's first call and main's two creates take t = 0..300; then one
    // step per 100-cycle yield, the last at t = 1000.
    EXPECT_EQ(n, 8);
}

TEST(RtosKernel, ThreadsThatNeverUseTimeAreAnError) {
    Rtos r;  // call_cycles 0: a yield loop never lets time pass
    r.start([&](Kernel& k) {
        k.create_thread([&] { for (;;) k.yield(); }, 0, "A");
        k.create_thread([&] { for (;;) k.yield(); }, 0, "B");
    });
    EXPECT_THROW(r.k.run_for(kMs), rtos::RtosError);
}

TEST(RtosKernel, DelaysWakeOnTicks) {
    Rtos r;
    int a = 0, b = 0;
    r.start([&](Kernel& k) {
        k.create_thread([&] { for (;;) { ++a; k.delay(10); } }, 0, "A");  // 1 tick
        k.create_thread([&] { for (;;) { ++b; k.delay(20); } }, 0, "B");  // 2 ticks
    });
    r.k.run_for(100 * kMs);
    EXPECT_EQ(a, 11) << "t=0, then ticks 1..10";
    EXPECT_EQ(b, 6) << "t=0, then ticks 2, 4, 6, 8, 10";
    EXPECT_EQ(r.k.idle_cycles(), 100 * kMs) << "zero-time threads: the processor idles throughout";
    EXPECT_EQ(r.k.tick_count(), 10u);
}

TEST(RtosKernel, DelayRoundsUpToWholeTicksAndZeroDoesNotBlock) {
    Rtos r;
    std::vector<std::uint64_t> at;
    r.start([&](Kernel& k) {
        k.create_thread(
            [&] {
                EXPECT_EQ(k.delay(0), Status::Ok);
                at.push_back(r.mcu.cycles());
                EXPECT_EQ(k.delay(1), Status::EventTimeout);  // 1 ms: one tick
                at.push_back(r.mcu.cycles());
                k.delay(25);  // 3 ticks
                at.push_back(r.mcu.cycles());
            },
            0, "A");
    });
    r.k.run_for(100 * kMs);
    EXPECT_EQ(at, (std::vector<std::uint64_t>{0, kFirstTick, kFirstTick + 3 * kTick}));
}

// rt_put_dly puts a delay that ends on the same tick as others before them, so
// they wake latest-first.
TEST(RtosKernel, SimultaneousWakeupsAreLatestFirst) {
    Rtos r;
    std::vector<char> order;
    r.start([&](Kernel& k) {
        for (char name : {'C', 'D', 'E'})
            k.create_thread([&, name] { k.delay(10); order.push_back(name); }, 0, std::string(1, name));
    });
    r.k.run_for(20 * kMs);
    EXPECT_EQ(std::string(order.begin(), order.end()), "EDC");
}

TEST(RtosKernel, SignalWaitForAllFlags) {
    Rtos r;
    rtos::Event got;
    std::uint64_t woke = 0;
    ThreadId waiter = 0;
    r.start([&](Kernel& k) {
        waiter = k.create_thread(
            [&] {
                got = k.signal_wait(0x3, rtos::kWaitForever);
                woke = r.mcu.cycles();
            },
            1, "W");
        k.create_thread(
            [&] {
                EXPECT_EQ(k.signal_set(waiter, 0x1), 0);
                k.consume(5 * kMs);
                EXPECT_EQ(k.signal_set(waiter, 0x2), 0x1) << "previous flags";
            },
            0, "S");
    });
    r.k.run_for(10 * kMs);
    EXPECT_EQ(got.status, Status::EventSignal);
    EXPECT_EQ(got.signals, 0x3);
    EXPECT_EQ(woke, 5 * kMs);
    EXPECT_EQ(r.k.thread(waiter)->signals, 0) << "the flags it waited for are cleared";
}

TEST(RtosKernel, SignalWaitForAnyFlagReturnsTheFlagsThatCame) {
    Rtos r;
    rtos::Event got;
    ThreadId waiter = 0;
    r.start([&](Kernel& k) {
        waiter = k.create_thread([&] { got = k.signal_wait(0, rtos::kWaitForever); }, 1, "W");
        k.create_thread([&] { k.signal_set(waiter, 0x14); }, 0, "S");
    });
    r.k.run_for(kMs);
    EXPECT_EQ(got.status, Status::EventSignal);
    EXPECT_EQ(got.signals, 0x14);
}

TEST(RtosKernel, SignalWaitTimesOutAndPollingDoesNotBlock) {
    Rtos r;
    rtos::Event timed, polled;
    std::uint64_t at = 0;
    r.start([&](Kernel& k) {
        k.create_thread(
            [&] {
                polled = k.signal_wait(0x1, 0);
                timed = k.signal_wait(0x1, 20);
                at = r.mcu.cycles();
            },
            0, "W");
    });
    r.k.run_for(50 * kMs);
    EXPECT_EQ(polled.status, Status::Ok);
    EXPECT_EQ(timed.status, Status::EventTimeout);
    EXPECT_EQ(at, kFirstTick + kTick);
}

TEST(RtosKernel, SignalFlagsAreSixteenBits) {
    Rtos r;
    ThreadId t = 0;
    std::int32_t result = 0;
    rtos::Event bad;
    r.start([&](Kernel& k) {
        t = k.create_thread(
            [&] {
                result = k.signal_set(t, 0x10000);
                bad = k.signal_wait(0x10000, 0);
            },
            0, "T");
    });
    r.k.run_for(1);
    EXPECT_EQ(result, static_cast<std::int32_t>(0x80000000u));
    EXPECT_EQ(bad.status, Status::ErrorValue);
}

// An interrupt handler's osSignalSet takes effect after the handler, as RTX's
// PendSV does: the woken thread preempts the running one at that instant.
TEST(RtosKernel, SignalFromAnInterruptWakesAThreadAfterTheHandler) {
    Rtos r;
    ThreadId waiter = 0;
    std::uint64_t woke = 0;
    int low_steps = 0;
    r.mcu.bind_handler(lpc17xx::kTimer0Irq, [&] {
        r.mcu.write32(0x40004000, 1);  // T0IR: clear MR0
        r.k.signal_set(waiter, 0x1);
    });
    r.start([&](Kernel& k) {
        waiter = k.create_thread(
            [&] {
                k.signal_wait(0x1, rtos::kWaitForever);
                woke = r.mcu.cycles();
            },
            1, "W");
        k.create_thread([&] { for (;;) { k.consume(kMs); ++low_steps; } }, 0, "L");
    });
    // TIMER0 at CCLK, one match 12,345 cycles from now.
    r.mcu.write32(0x400FC1A8, 1u << 2);   // PCLKSEL0: TIMER0 = CCLK
    r.mcu.write32(0x40004018, 12'344);   // MR0
    r.mcu.write32(0x40004014, 5);        // MCR: interrupt, stop
    r.mcu.write32(0xE000E100, 1u << 1);  // ISER0: TIMER0
    r.mcu.write32(0x40004004, 1);        // TCR: run
    r.k.run_for(3 * kMs);
    EXPECT_EQ(woke, 12'345u);
    EXPECT_EQ(low_steps, 3) << "the low thread lost the processor only for zero time";
}

TEST(RtosKernel, MutexSerialisesAndHandsOverInPriorityOrder) {
    Rtos r;
    std::vector<std::string> log;
    rtos::MutexId m = 0;
    r.start([&](Kernel& k) {
        m = k.create_mutex();
        k.create_thread(
            [&] {
                k.mutex_wait(m, rtos::kWaitForever);
                log.push_back("L in");
                k.consume(15 * kMs);
                log.push_back("L out");
                k.mutex_release(m);
            },
            -1, "L");
        for (const char* name : {"N1", "N2"})
            k.create_thread(
                [&, name] {
                    k.delay(10);
                    EXPECT_EQ(k.mutex_wait(m, rtos::kWaitForever), Status::Ok);
                    log.push_back(std::string(name) + " in");
                    k.mutex_release(m);
                },
                0, name);
    });
    r.k.run_for(50 * kMs);
    EXPECT_EQ(log, (std::vector<std::string>{"L in", "L out", "N2 in", "N1 in"}))
        << "N2 woke first (latest delay first) and queued first";
}

TEST(RtosKernel, MutexIsRecursiveAndOnlyTheOwnerReleases) {
    Rtos r;
    rtos::MutexId m = 0;
    Status other_release = Status::Ok, extra_release = Status::Ok, poll = Status::Ok;
    r.start([&](Kernel& k) {
        m = k.create_mutex();
        k.create_thread(
            [&] {
                k.mutex_wait(m, 0);
                k.mutex_wait(m, 0);
                k.delay(10);
                k.mutex_release(m);
                k.mutex_release(m);
                extra_release = k.mutex_release(m);
            },
            0, "Owner");
        k.create_thread(
            [&] {
                other_release = k.mutex_release(m);
                poll = k.mutex_wait(m, 0);
            },
            0, "Other");
    });
    r.k.run_for(30 * kMs);
    EXPECT_EQ(other_release, Status::ErrorResource);
    EXPECT_EQ(poll, Status::ErrorResource) << "owned, no timeout";
    EXPECT_EQ(extra_release, Status::ErrorResource) << "unbalanced";
}

TEST(RtosKernel, MutexWaitTimesOut) {
    Rtos r;
    rtos::MutexId m = 0;
    Status timed = Status::Ok;
    std::uint64_t at = 0;
    r.start([&](Kernel& k) {
        m = k.create_mutex();
        k.create_thread([&] { k.mutex_wait(m, 0); k.delay(1000); }, 0, "Holder");
        k.create_thread(
            [&] {
                timed = k.mutex_wait(m, 20);
                at = r.mcu.cycles();
            },
            0, "Waiter");
    });
    r.k.run_for(50 * kMs);
    EXPECT_EQ(timed, Status::ErrorTimeoutResource);
    EXPECT_EQ(at, kFirstTick + kTick);
    EXPECT_TRUE(r.k.mutexes()[0].waiters.empty());
}

// Low holds the mutex; High waits for it; Medium is ready. Low inherits High's
// priority, finishes before Medium runs, and drops back when it releases.
TEST(RtosKernel, PriorityInheritanceLetsTheOwnerFinishFirst) {
    Rtos r;
    rtos::MutexId m = 0;
    ThreadId tl = 0, tm = 0, th = 0;
    int level_while_waited = 0;
    std::vector<std::string> log;
    r.start([&](Kernel& k) {
        m = k.create_mutex();
        tl = k.create_thread(
            [&] {
                k.mutex_wait(m, rtos::kWaitForever);
                k.consume(5 * kMs);  // until after the first tick
                k.consume(5 * kMs);
                level_while_waited = k.thread(tl)->level;
                k.consume(20 * kMs);
                log.push_back("L release");
                k.mutex_release(m);
                log.push_back("L done");
            },
            -2, "Low");
        tm = k.create_thread([&] { k.delay(10); log.push_back("M runs"); k.consume(30 * kMs); }, 0, "Medium");
        th = k.create_thread(
            [&] {
                k.delay(10);
                k.mutex_wait(m, rtos::kWaitForever);
                log.push_back("H has it");
                k.mutex_release(m);
            },
            2, "High");
    });
    r.k.run_for(100 * kMs);
    EXPECT_EQ(level_while_waited, 6) << "High";
    EXPECT_EQ(r.k.thread(tl)->level, 2) << "back to Low";
    EXPECT_EQ(log, (std::vector<std::string>{"L release", "H has it", "M runs", "L done"}));
    EXPECT_EQ(r.rtos_trace().size() > 0, true);
    bool inherit = false, restore = false;
    for (const auto& line : r.rtos_trace()) {
        inherit |= line.find("inherit High M1") != std::string::npos;
        restore |= line.find("restore Low M1") != std::string::npos;
    }
    EXPECT_TRUE(inherit);
    EXPECT_TRUE(restore);
}

TEST(RtosKernel, SetPriorityTakesEffectAtOnce) {
    Rtos r;
    std::vector<std::string> log;
    ThreadId a = 0, b = 0;
    r.start([&](Kernel& k) {
        a = k.create_thread(
            [&] {
                log.push_back("A");
                k.set_priority(a, 2);  // raising itself: keeps running
                log.push_back("A high");
                k.set_priority(a, -1);  // below B: B runs now
                log.push_back("A low");
            },
            0, "A");
        b = k.create_thread(
            [&] {
                log.push_back("B");
                EXPECT_EQ(k.priority(b), 0);
            },
            0, "B");
    });
    r.k.run_for(1);
    EXPECT_EQ(log, (std::vector<std::string>{"A", "A high", "B", "A low"}));
}

TEST(RtosKernel, RaisingAReadyThreadAboveTheRunningOnePreemptsIt) {
    Rtos r;
    std::vector<std::string> log;
    ThreadId b = 0;
    r.start([&](Kernel& k) {
        k.create_thread(
            [&] {
                log.push_back("A before");
                k.set_priority(b, 1);
                log.push_back("A after");
            },
            0, "A");
        b = k.create_thread([&] { log.push_back("B"); }, 0, "B");
    });
    r.k.run_for(1);
    EXPECT_EQ(log, (std::vector<std::string>{"A before", "B", "A after"}));
}

TEST(RtosKernel, SetPriorityChecksItsArguments) {
    Rtos r;
    Status bad_thread = Status::Ok, bad_value = Status::Ok;
    r.start([&](Kernel& k) {
        k.create_thread(
            [&] {
                bad_thread = k.set_priority(99, 0);
                bad_value = k.set_priority(k.current_thread(), 4);
            },
            0, "A");
    });
    r.k.run_for(1);
    EXPECT_EQ(bad_thread, Status::ErrorParameter);
    EXPECT_EQ(bad_value, Status::ErrorValue);
}

TEST(RtosKernel, TerminatingAnotherThread) {
    Rtos r;
    int victim_steps = 0;
    ThreadId v = 0;
    Status twice = Status::Ok;
    r.start([&](Kernel& k) {
        v = k.create_thread([&] { for (;;) { ++victim_steps; k.delay(10); } }, 0, "Victim");
        k.create_thread(
            [&] {
                k.delay(25);
                EXPECT_EQ(k.terminate(v), Status::Ok);
                twice = k.terminate(v);
            },
            0, "Killer");
    });
    r.k.run_for(100 * kMs);
    // t=0 and ticks 1-3: at tick 3 both wake, the victim first (delayed later).
    EXPECT_EQ(victim_steps, 4);
    EXPECT_EQ(twice, Status::ErrorParameter);
    EXPECT_EQ(r.k.thread(v)->state, ThreadState::Terminated);
}

TEST(RtosKernel, TimersFireOnTicksAndCallBackOnTheTimerThread) {
    Rtos r;
    std::vector<std::pair<char, std::uint64_t>> calls;
    ThreadId in_callback = 0;
    r.start([&](Kernel& k) {
        const auto periodic = k.create_timer(
            [&] {
                calls.emplace_back('P', r.mcu.cycles());
                in_callback = k.current_thread();
            },
            true);
        const auto once = k.create_timer([&] { calls.emplace_back('O', r.mcu.cycles()); }, false);
        k.timer_start(periodic, 30);
        k.timer_start(once, 50);
    });
    r.k.run_for(100 * kMs);
    const auto t = [](unsigned tick) { return kFirstTick + (tick - 1) * kTick; };
    EXPECT_EQ(calls, (std::vector<std::pair<char, std::uint64_t>>{
                         {'P', t(3)}, {'O', t(5)}, {'P', t(6)}, {'P', t(9)}}));
    EXPECT_EQ(in_callback, 1u) << "osTimerThread";
    const auto timers = r.k.timers();
    ASSERT_EQ(timers.size(), 2u);
    EXPECT_TRUE(timers[0].running);
    EXPECT_FALSE(timers[1].running) << "one-shot stopped";
}

TEST(RtosKernel, TimerStopAndRestart) {
    Rtos r;
    std::vector<std::uint64_t> fired;
    Status stop_again = Status::Ok;
    r.start([&](Kernel& k) {
        const auto t = k.create_timer([&] { fired.push_back(r.k.tick_count()); }, true);
        k.create_thread(
            [&, t] {
                k.timer_start(t, 20);
                k.delay(50);                // fires at ticks 2, 4
                k.timer_stop(t);
                stop_again = k.timer_stop(t);
                k.delay(30);
                k.timer_start(t, 10);       // stopped: new period 1 tick
            },
            0, "Ctl");
    });
    r.k.run_for(100 * kMs);
    EXPECT_EQ(stop_again, Status::ErrorResource);
    EXPECT_EQ(fired, (std::vector<std::uint64_t>{2, 4, 9, 10}));
}

TEST(RtosKernel, TimerQueueOverflowIsAnError) {
    Config c;
    c.timer_priority = rtos::kPriorityIdle;  // never gets the processor below a busy thread
    Rtos r(c);
    r.start([&](Kernel& k) {
        for (int i = 0; i < 5; ++i) k.timer_start(k.create_timer([] {}, false), 10);
        k.create_thread([&] { for (;;) k.consume(kMs); }, 0, "Busy");
    });
    EXPECT_THROW(r.k.run_for(20 * kMs), rtos::RtosError);
}

TEST(RtosKernel, IdleTimeIsAccounted) {
    Rtos r;
    r.start([&](Kernel& k) {
        k.create_thread([&] { for (;;) { k.consume(3 * kMs); k.delay(10); } }, 0, "Worker");
    });
    r.k.run_for(100 * kMs);
    // Each tick period: 3 ms of work after waking, the rest idle.
    EXPECT_EQ(r.k.idle_cycles() + r.k.thread(3)->run_cycles, 100 * kMs);
    EXPECT_EQ(r.k.thread(3)->run_cycles, 3 * kMs * 10) << "t=0 and ticks 1-9; tick 10 is the end of the run";
}

TEST(RtosKernel, BlockingCallsOutsideAThreadAreErrors) {
    Rtos r;
    r.k.initialize();
    EXPECT_THROW(r.k.delay(10), rtos::RtosError);
    EXPECT_THROW(r.k.consume(10), rtos::RtosError);
}

TEST(RtosKernel, RepeatedRunsAreIdentical) {
    auto run = [] {
        Rtos r(round_robin(1));
        r.start([&](Kernel& k) {
            const auto m = k.create_mutex();
            k.create_thread([&, m] { for (;;) { k.mutex_wait(m, rtos::kWaitForever); k.consume(3 * kMs); k.mutex_release(m); k.delay(10); } }, 0, "A");
            k.create_thread([&, m] { for (;;) { k.mutex_wait(m, rtos::kWaitForever); k.consume(7 * kMs); k.mutex_release(m); } }, 0, "B");
            k.timer_start(k.create_timer([] {}, true), 30);
        });
        r.k.run_for(200 * kMs);
        return std::pair{r.mcu.trace().events(), r.k.timeline()};
    };
    const auto first = run();
    const auto second = run();
    EXPECT_EQ(first.first, second.first);
    EXPECT_EQ(first.second.size(), second.second.size());
    for (std::size_t i = 0; i < first.second.size(); ++i) EXPECT_EQ(first.second[i], second.second[i]);
}
