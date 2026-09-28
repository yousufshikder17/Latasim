// The workbench session layer (workbench/session.hpp): every built-in scenario
// against its own check, fault capture, and the media-center application driven
// through its menu the way the desktop drives it.
#include "workbench/session.hpp"
#include "workbench/views.hpp"

#include "LPC17xx.h"
#include "cmsis_os.h"
#include "latasim_rtos.h"
#include "host/binding.hpp"
#include "media_center.h"
#include "usb_speaker.h"

#include <gtest/gtest.h>

#include <set>
#include <string>

using namespace latasim;
using latasim::mcb1700::JoystickDirection;
using latasim::workbench::Session;

namespace {

constexpr std::uint64_t kMs = 100'000;

std::size_t index_of(const std::string& name) {
    const auto& all = workbench::scenarios();
    for (std::size_t i = 0; i < all.size(); ++i)
        if (all[i].name == name) return i;
    throw std::logic_error("no scenario " + name);
}

void tap(Session& s, JoystickDirection d) {
    s.input([d](mcb1700::Board& b) { b.press(d); });
    s.run_for(40 * kMs);
    s.input([d](mcb1700::Board& b) { b.release(d); });
    s.run_for(40 * kMs);
}

}  // namespace

class EveryScenario : public testing::TestWithParam<std::size_t> {};

TEST_P(EveryScenario, PassesItsCheck) {
    Session s(GetParam());
    s.run_until(s.scenario().check_after);
    const auto check = s.check();
    EXPECT_FALSE(s.faulted()) << s.fault();
    EXPECT_TRUE(check.passed) << s.scenario().name << ": " << check.detail;
}

INSTANTIATE_TEST_SUITE_P(Workbench, EveryScenario, testing::Range<std::size_t>(0, workbench::scenarios().size()));

TEST(Workbench, ScenariosCanRunAgainInOneProcess) {
    for (int run = 0; run < 2; ++run) {
        Session s(index_of("RTOS: rate-monotonic"));
        s.run_until(s.scenario().check_after);
        EXPECT_TRUE(s.check().passed) << s.check().detail;
    }
}

TEST(Workbench, FirmwareFaultsStopTheSessionNotTheProcess) {
    Session s(index_of("Blinky_ULp"));
    s.run_for(5 * kMs);
    s.input([](mcb1700::Board&) { latasim_mmio_read32(0x4000C000); });  // UART0: not modelled
    EXPECT_TRUE(s.faulted());
    EXPECT_NE(s.fault().find("bus fault"), std::string::npos) << s.fault();
    const auto t = s.now();
    s.run_for(10 * kMs);
    EXPECT_EQ(s.now(), t) << "a faulted session does nothing";
}

TEST(Workbench, RtosThreadFaultsStopTheSession) {
    Session s(index_of("RTOS: delays"));
    s.run_for(5 * kMs);
    ASSERT_FALSE(s.faulted());
    // A kernel-call error from a thread arrives through run_until.
    s.kernel()->create_thread([] { osSemaphoreCreate(nullptr, 0); }, 3, "bad");
    s.run_for(1 * kMs);
    EXPECT_TRUE(s.faulted());
    EXPECT_NE(s.fault().find("osSemaphoreCreate"), std::string::npos) << s.fault();
}

TEST(Workbench, StepGoesToTheNextEvent) {
    Session s(index_of("Blinky_ULp"));
    s.step();
    EXPECT_EQ(s.now(), 999'999u) << "the first SysTick count to 0";
    s.step();
    EXPECT_EQ(s.now(), 999'999u + 1'300u) << "the ADC conversion it started";
}

TEST(MediaCenter, MenuPhotosGameAndAudioWithUsb) {
    Session s(index_of("Media center"));
    s.run_for(50 * kMs);
    EXPECT_EQ(media.screen, MEDIA_MENU);
    EXPECT_EQ(s.board().led(0), mcb1700::LedState::On) << "LED shows the selection";

    tap(s, JoystickDirection::Center);  // photo gallery
    EXPECT_EQ(media.screen, MEDIA_PHOTOS);
    const auto first = s.board().glcd().hash();
    tap(s, JoystickDirection::Right);
    EXPECT_EQ(media.photo, 1u);
    EXPECT_NE(s.board().glcd().hash(), first) << "another picture";
    tap(s, JoystickDirection::Center);
    EXPECT_EQ(media.screen, MEDIA_MENU);

    tap(s, JoystickDirection::Down);
    EXPECT_EQ(media.selection, 1u);
    EXPECT_EQ(s.board().led(1), mcb1700::LedState::On);
    s.input([](mcb1700::Board& b) { b.set_potentiometer(0x800); });
    tap(s, JoystickDirection::Center);  // audio player: connects as a USB speaker
    s.run_for(200 * kMs);
    EXPECT_EQ(media.screen, MEDIA_AUDIO);
    EXPECT_EQ(s.pc()->state(), usb::AudioHost::State::Streaming) << s.pc()->state_name();
    EXPECT_TRUE(usb_speaker.playing);
    EXPECT_EQ(media.volume, 0x80u) << "potentiometer 0x800 -> volume 128";
    EXPECT_EQ(usb_speaker.volume, 0x80u);
    s.input([](mcb1700::Board& b) { b.set_potentiometer(0xFFF); });
    s.run_for(60 * kMs);
    EXPECT_EQ(usb_speaker.volume, 0xFFu) << "turning the potentiometer changes the firmware's volume";
    tap(s, JoystickDirection::Center);  // back: disconnects
    s.run_for(5 * kMs);
    EXPECT_EQ(media.screen, MEDIA_MENU);
    EXPECT_EQ(s.pc()->state(), usb::AudioHost::State::Detached);
    EXPECT_FALSE(usb_speaker.connected);

    tap(s, JoystickDirection::Down);
    tap(s, JoystickDirection::Center);  // paddle game
    EXPECT_EQ(media.screen, MEDIA_GAME);
    const auto start = media.paddle_x;
    s.input([](mcb1700::Board& b) { b.press(JoystickDirection::Right); });
    s.run_for(100 * kMs);
    s.input([](mcb1700::Board& b) { b.release(JoystickDirection::Right); });
    EXPECT_GT(media.paddle_x, start);
    s.run_for(2000 * kMs);
    EXPECT_GT(media.score + media.misses, 0u) << "the ball comes down";
    EXPECT_FALSE(s.faulted()) << s.fault();
}

TEST(MediaCenter, RepeatedRunsAreIdentical) {
    auto run = [] {
        Session s(index_of("Media center"));
        s.run_for(30 * kMs);
        tap(s, JoystickDirection::Down);
        tap(s, JoystickDirection::Down);
        tap(s, JoystickDirection::Center);
        s.run_for(300 * kMs);
        return std::pair{s.board().glcd().hash(), s.board().mcu().trace().events().size()};
    };
    EXPECT_EQ(run(), run());
}

TEST(WorkbenchViews, RegistersReadWithoutSideEffects) {
    Session s(index_of("Blinky_ULp"));
    s.run_for(25 * kMs);
    const auto before = s.board().mcu().trace().events().size();
    const auto stctrl = workbench::register_value(s, 0xE000E010);
    for (const auto& group : workbench::register_groups())
        for (const auto& r : group.registers) workbench::register_value(s, r.address);  // none faults
    EXPECT_EQ(s.board().mcu().trace().events().size(), before) << "inspection records nothing";
    EXPECT_EQ(workbench::register_value(s, 0xE000E010), stctrl) << "COUNTFLAG is not cleared by looking";
}

TEST(WorkbenchViews, TimelineHasEachThreadsRunsAndTheInterrupts) {
    Session s(index_of("RTOS: rate-monotonic"));
    s.run_for(400 * kMs);
    const auto t = workbench::timeline(s);
    ASSERT_EQ(t.threads.size(), s.kernel()->threads().size());
    EXPECT_EQ(t.threads[0].name, "os_idle_demon");
    std::uint64_t total = 0;
    for (const auto& row : t.threads)
        for (const auto& i : row.intervals) total += i.end - i.start;
    EXPECT_EQ(total, s.now()) << "every cycle is some thread's or the idle demon's";
    EXPECT_FALSE(t.interrupts.empty()) << "SysTick entries";
    EXPECT_EQ(t.interrupts.front().label, "SysTick");
}

TEST(WorkbenchViews, TraceEventsHaveCategories) {
    Session s(index_of("USB speaker"));
    s.run_for(50 * kMs);
    std::set<std::string> seen;
    for (const auto& e : s.board().mcu().trace().events()) seen.insert(describe(e).category);
    for (const char* c : {"interrupt", "timer", "usb/audio"}) EXPECT_TRUE(seen.count(c)) << c;
}

// A bare-metal main() (Scenario::bare_main) runs on its own fiber whenever virtual
// time has caught up with what it consumed; external firmware runs this way.
namespace {

workbench::Scenario bare(std::function<void()> main) {
    workbench::Scenario s;
    s.name = "bare-metal test";
    s.bare_main = std::move(main);
    return s;
}

}  // namespace

TEST(Workbench, BareMainStoresHappenWhenTimeHasCaughtUp) {
    Session s(bare([] {
        LPC_GPIO1->FIODIR |= 1UL << 28;
        latasim_consume_cycles(1'000);
        LPC_GPIO1->FIOSET = 1UL << 28;
    }));
    EXPECT_EQ(s.board().led(0), mcb1700::LedState::Off) << "set-up before the first consume runs at once";
    s.run_until(999);
    EXPECT_EQ(s.board().led(0), mcb1700::LedState::Off);
    s.run_until(1'000);
    EXPECT_EQ(s.board().led(0), mcb1700::LedState::On);
    s.run_for(4'000);
    EXPECT_FALSE(s.faulted()) << "returning from main leaves the processor idle: " << s.fault();
    EXPECT_EQ(s.now(), 5'000u);
}

TEST(Workbench, BareMainThatConsumesWithoutAccessesStillAdvances) {
    Session s(bare([] {
        for (;;) latasim_consume_cycles(7);
    }));
    s.run_for(3 * kMs + 5);
    EXPECT_FALSE(s.faulted()) << s.fault();
    EXPECT_EQ(s.now(), 3 * kMs + 5);
}

TEST(Workbench, BareMainFaultsStopTheSession) {
    Session s(bare([] {
        latasim_consume_cycles(50);
        LATASIM_REG32(0x10000000) = 1;
    }));
    EXPECT_FALSE(s.faulted()) << "the store is 50 cycles in";
    s.run_for(100);
    ASSERT_TRUE(s.faulted());
    EXPECT_NE(s.fault().find("bus fault: no register at 0x10000000"), std::string::npos) << s.fault();
    EXPECT_EQ(s.now(), 50u);
    s.run_for(100);
    EXPECT_EQ(s.now(), 50u) << "a faulted session does nothing further";
}
