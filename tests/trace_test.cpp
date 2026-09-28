// The deterministic hardware trace (src/trace/trace.hpp).
#include "boards/mcb1700/board.hpp"
#include "boards/mcb1700/keil_board_led.hpp"
#include "gpio_snapshot.hpp"
#include "lpc17xx/keil_gpio_driver.hpp"
#include "lpc17xx/lpc1768.hpp"
#include "trace/trace.hpp"

#include <gtest/gtest.h>

#include <vector>

using latasim::Trace;
using latasim::TraceEvent;
using latasim::TraceKind;
using latasim::lpc17xx::bit_band_alias;
using latasim::lpc17xx::BusFault;
using latasim::lpc17xx::KeilGpioDriver;
using latasim::mcb1700::Board;
using latasim::mcb1700::JoystickDirection;
using latasim::mcb1700::KeilBoardLed;
using latasim::mcb1700::LedState;
using latasim::test::snapshot;

namespace {

constexpr std::uint32_t FIO1DIR = 0x2009C020;
constexpr std::uint32_t FIO1PIN = 0x2009C034;
constexpr std::uint32_t FIO1SET = 0x2009C038;
constexpr std::uint32_t FIO1CLR = 0x2009C03C;

TraceEvent read(std::uint64_t seq, std::uint32_t address, unsigned width, std::uint32_t value) {
    return {.seq = seq, .kind = TraceKind::Read, .address = address, .width = width, .value = value};
}
TraceEvent write(std::uint64_t seq, std::uint32_t address, unsigned width, std::uint32_t value) {
    return {.seq = seq, .kind = TraceKind::Write, .address = address, .width = width, .value = value};
}
TraceEvent input(std::uint64_t seq, unsigned port, unsigned pin, bool high) {
    return {.seq = seq, .kind = TraceKind::Input, .value = high ? 1u : 0u, .port = port, .pin = pin};
}
TraceEvent led(std::uint64_t seq, unsigned index, LedState state) {
    return {.seq = seq, .kind = TraceKind::Led, .value = static_cast<std::uint32_t>(state), .led = index};
}

// A short firmware-like run: LED0 on and off, a joystick press read back.
std::vector<TraceEvent> scripted_run() {
    Board board;
    KeilBoardLed leds(board.mcu());
    KeilGpioDriver gpio(board.mcu());
    leds.initialize();
    leds.on(0);
    board.press(JoystickDirection::Up);
    gpio.pin_read(1, 23);
    board.release(JoystickDirection::Up);
    leds.off(0);
    return board.mcu().trace().events();
}

}  // namespace

TEST(Trace, SequenceNumbersStartAtOneAndIncreaseByOne) {
    const auto events = scripted_run();
    ASSERT_FALSE(events.empty());
    for (std::size_t i = 0; i < events.size(); ++i) EXPECT_EQ(events[i].seq, i + 1);
}

TEST(Trace, WritesRecordAddressWidthAndValue) {
    Board board;
    board.mcu().write32(FIO1DIR, 0x10000000);
    board.mcu().write8(FIO1SET + 3, 0x10);
    board.mcu().write16(FIO1CLR + 2, 0x1000);
    const std::vector<TraceEvent> expected = {
        write(1, FIO1DIR, 4, 0x10000000), led(2, 0, LedState::Off),  write(3, FIO1SET + 3, 1, 0x10),
        led(4, 0, LedState::On),          write(5, FIO1CLR + 2, 2, 0x1000), led(6, 0, LedState::Off),
    };
    EXPECT_EQ(board.mcu().trace().events(), expected);
}

TEST(Trace, ReadsRecordTheValueReturned) {
    Board board;
    const std::uint32_t pin = board.mcu().read32(FIO1PIN);
    const std::uint8_t byte = board.mcu().read8(FIO1PIN + 2);
    EXPECT_EQ(board.mcu().trace().events(),
              (std::vector<TraceEvent>{read(1, FIO1PIN, 4, pin), read(2, FIO1PIN + 2, 1, byte)}));
}

TEST(Trace, BitBandAccessIsOneEventAtTheAliasAddress) {
    Board board;
    const std::uint32_t alias = bit_band_alias(FIO1DIR, 28);
    board.mcu().write32(alias, 1);
    EXPECT_EQ(board.mcu().read32(alias), 1u);
    EXPECT_EQ(board.mcu().trace().events(),
              (std::vector<TraceEvent>{write(1, alias, 4, 1), led(2, 0, LedState::Off), read(3, alias, 4, 1)}));
}

TEST(Trace, FaultingAccessRecordsNothing) {
    Board board;
    EXPECT_THROW(board.mcu().write32(0x2009C004, 1), BusFault);
    EXPECT_THROW(board.mcu().read16(FIO1PIN + 1), BusFault);
    EXPECT_TRUE(board.mcu().trace().events().empty());
}

TEST(Trace, JoystickInputsAreTracedOnChangeOnly) {
    Board board;
    board.press(JoystickDirection::Up);
    board.press(JoystickDirection::Up);  // already held: no change, no event
    board.press(JoystickDirection::Left);
    board.release(JoystickDirection::Up);
    board.release(JoystickDirection::Up);
    EXPECT_EQ(board.mcu().trace().events(),
              (std::vector<TraceEvent>{input(1, 1, 23, false), input(2, 1, 26, false), input(3, 1, 23, true)}));
}

TEST(Trace, Int0InputsAreTraced) {
    Board board;
    board.press_int0();
    board.release_int0();
    EXPECT_EQ(board.mcu().trace().events(),
              (std::vector<TraceEvent>{input(1, 2, 10, false), input(2, 2, 10, true)}));
}

TEST(Trace, NewBoardStartsEmptyWithReleasedInputs) {
    Board board;
    EXPECT_TRUE(board.mcu().trace().events().empty()) << "reset state is not an event";
}

TEST(Trace, LedChangeFollowsTheStoreThatCausedIt) {
    Board board;
    KeilBoardLed leds(board.mcu());
    leds.initialize();
    const auto before = board.mcu().trace().events().size();
    leds.on(0);
    const auto& events = board.mcu().trace().events();
    ASSERT_EQ(events.size(), before + 2);
    EXPECT_EQ(events[before], write(before + 1, FIO1SET, 4, 1u << 28));
    EXPECT_EQ(events[before + 1], led(before + 2, 0, LedState::On));
}

TEST(Trace, IdenticalRunsGiveIdenticalTraces) {
    const auto first = scripted_run();
    const auto second = scripted_run();
    EXPECT_EQ(first, second);
    std::string a, b;
    for (const auto& e : first) a += to_string(e) + "\n";
    for (const auto& e : second) b += to_string(e) + "\n";
    EXPECT_EQ(a, b) << "formatted traces are byte-identical too";
}

TEST(Trace, RecordingDoesNotChangeModelBehaviour) {
    Board traced, untraced;
    untraced.mcu().trace().set_enabled(false);
    for (Board* board : {&traced, &untraced}) {
        KeilBoardLed leds(board->mcu());
        leds.initialize();
        leds.set_out(0xA5);
        board->press(JoystickDirection::Down);
        board->mcu().write32(bit_band_alias(FIO1PIN, 28), 0);
    }
    EXPECT_EQ(snapshot(traced), snapshot(untraced));
    EXPECT_FALSE(traced.mcu().trace().events().empty());
    EXPECT_TRUE(untraced.mcu().trace().events().empty());
}

TEST(Trace, FormatsEachKindOnOneLine) {
    EXPECT_EQ(to_string(write(1, FIO1SET, 4, 0x10000000)), "#1    t=0          write32 FIO1SET   0x10000000");
    EXPECT_EQ(to_string(read(12, FIO1PIN + 3, 1, 0x4F)), "#12   t=0          read8   FIO1PIN3  0x4F");
    EXPECT_EQ(to_string(read(3, FIO1PIN + 2, 2, 0xFF6F)), "#3    t=0          read16  FIO1PINH  0xFF6F");
    EXPECT_EQ(to_string(write(4, latasim::lpc17xx::kPconpAddress, 4, 0)),
              "#4    t=0          write32 PCONP     0x00000000");
    EXPECT_EQ(to_string(write(5, 0x233806EC, 4, 0)), "#5    t=0          write32 0x233806EC 0x00000000");
    EXPECT_EQ(to_string(input(6, 1, 23, false)), "#6    t=0          input   P1.23     low");
    EXPECT_EQ(to_string(led(7, 0, LedState::On)), "#7    t=0          led     LED0      ON");
    TraceEvent tick{.seq = 8, .cycles = 999'999, .kind = TraceKind::Interrupt, .value = latasim::kInterruptEnter,
                    .irq = latasim::lpc17xx::kSysTickIrq};
    EXPECT_EQ(to_string(tick), "#8    t=999999     irq     SysTick   enter");
    TraceEvent adc{.seq = 10, .kind = TraceKind::Interrupt, .value = latasim::kInterruptPend, .irq = 22};
    EXPECT_EQ(to_string(adc), "#10   t=0          irq     ADC       pend");
    TraceEvent late = led(9, 3, LedState::Off);
    late.cycles = 123'456'789'012;
    EXPECT_EQ(to_string(late), "#9    t=123456789012 led     LED3      OFF") << "long times widen the column";
}

// --- virtual time on events ---

TEST(Trace, EventsCarryTheVirtualTimeTheyHappenedAt) {
    Board board;
    board.mcu().write32(FIO1DIR, 1u << 28);  // t = 0
    board.mcu().advance_cycles(250);
    board.press(JoystickDirection::Up);      // t = 250
    board.mcu().advance_cycles(750);
    board.mcu().write32(FIO1SET, 1u << 28);  // t = 1000, and its LED change
    std::vector<std::uint64_t> times;
    for (const auto& e : board.mcu().trace().events()) times.push_back(e.cycles);
    EXPECT_EQ(times, (std::vector<std::uint64_t>{0, 0, 250, 1000, 1000}));
}

TEST(Trace, SequenceAndTimeAreBothMonotonicAndSameTimeKeepsOrder) {
    Board board;
    KeilBoardLed leds(board.mcu());
    for (int step = 0; step < 50; ++step) {
        leds.set_out(static_cast<std::uint32_t>(step));
        if (step % 3 == 0) board.press(JoystickDirection::Left);
        if (step % 3 == 1) board.release(JoystickDirection::Left);
        board.mcu().advance_cycles(static_cast<std::uint64_t>(step % 4));  // sometimes 0
    }
    const auto& events = board.mcu().trace().events();
    ASSERT_GT(events.size(), 100u);
    for (std::size_t i = 1; i < events.size(); ++i) {
        EXPECT_EQ(events[i].seq, events[i - 1].seq + 1);
        EXPECT_GE(events[i].cycles, events[i - 1].cycles);
    }
}

TEST(Trace, NewMachineRestartsSequenceAndTime) {
    Board first;
    first.mcu().advance_cycles(1'000);
    first.mcu().write32(FIO1DIR, 0);
    Board second;
    second.mcu().write32(FIO1DIR, 0);
    EXPECT_EQ(first.mcu().trace().events().front().cycles, 1'000u);
    EXPECT_EQ(second.mcu().trace().events().front(), write(1, FIO1DIR, 4, 0)) << "seq 1, t = 0";
}

TEST(Trace, RecordingDoesNotChangeTimedBehaviour) {
    Board traced, untraced;
    untraced.mcu().trace().set_enabled(false);
    for (Board* board : {&traced, &untraced}) {
        KeilBoardLed leds(board->mcu());
        leds.initialize();
        int step = 0;
        board->mcu().bind_handler(latasim::lpc17xx::kSysTickIrq, [&leds, &step] { leds.set_out(1u << (step++ % 8)); });
        board->mcu().write32(0xE000E014, 999);                 // STRELOAD
        board->mcu().write32(0xE000E010, 0x7);                 // STCTRL: run with TICKINT
        board->mcu().advance_cycles(12'345);
    }
    EXPECT_EQ(snapshot(traced), snapshot(untraced));
    EXPECT_EQ(traced.mcu().cycles(), untraced.mcu().cycles());
    EXPECT_EQ(traced.mcu().systick().current(), untraced.mcu().systick().current());
}

// Retention: past the MMIO capacity the oldest half of the accesses go in one step;
// every other event stays, order and sequence numbers are kept, drops are counted.
TEST(Trace, OldMmioAccessesAreDroppedInBulkOtherEventsKept) {
    latasim::Trace trace;
    trace.set_mmio_capacity(4);
    const auto access = [&](std::uint32_t value) {
        trace.record({.kind = latasim::TraceKind::Read, .address = 0x2009C034, .width = 4, .value = value}, value);
    };
    access(1);
    trace.record({.kind = latasim::TraceKind::Led, .value = 1, .led = 0}, 1);
    access(2);
    access(3);
    access(4);
    EXPECT_EQ(trace.events().size(), 5u);
    EXPECT_EQ(trace.dropped(), 0u);
    access(5);  // five accesses held, capacity four: keep the newest two
    ASSERT_EQ(trace.events().size(), 3u);
    EXPECT_EQ(trace.dropped(), 3u);
    EXPECT_EQ(trace.events()[0].kind, latasim::TraceKind::Led) << "an LED event is never dropped";
    EXPECT_EQ(trace.events()[0].seq, 2u);
    EXPECT_EQ(trace.events()[1].value, 4u);
    EXPECT_EQ(trace.events()[2].value, 5u);
    EXPECT_EQ(trace.events()[2].seq, 6u) << "sequence numbers are not reused";
    for (std::uint32_t v = 6; v <= 100; ++v) access(v);
    std::size_t held = 0;
    for (const auto& e : trace.events()) held += e.kind == latasim::TraceKind::Read;
    EXPECT_LE(held, 4u);
    EXPECT_EQ(trace.dropped() + held, 100u);
}
