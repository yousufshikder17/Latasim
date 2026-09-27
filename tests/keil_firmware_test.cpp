// Keil's MCB1700 board-support firmware, compiled unchanged as C from the installed
// LPC1700_DFP and running on the host against Latasim:
//   LED_MCB1700.c, Joystick_MCB1700.c, Buttons_MCB1700.c
//     -> GPIO_* / PIN_* (src/host/keil_rte_gpio.cpp)
//     -> lpc17xx::Gpio -> mcb1700::Board
// The test plays the application: it calls the drivers' public API as firmware
// would and drives the board's physical inputs. It never sets LED state itself.
extern "C" {  // Keil's headers have no C++ guards; the drivers are compiled as C
#include "Board_Buttons.h"
#include "Board_Joystick.h"
#include "Board_LED.h"
}

#include "boards/mcb1700/keil_board_led.hpp"
#include "gpio_snapshot.hpp"
#include "host/binding.hpp"
#include "trace/trace.hpp"

#include <gtest/gtest.h>

#include <vector>

using latasim::host::FirmwareBinding;
using latasim::mcb1700::Board;
using latasim::mcb1700::JoystickDirection;
using latasim::mcb1700::kLedCount;
using latasim::mcb1700::LedState;
using latasim::test::snapshot;

namespace {

constexpr std::uint32_t FIO1DIR = 0x2009C020;
constexpr std::uint32_t FIO1PIN = 0x2009C034;
constexpr std::uint32_t FIO2DIR = 0x2009C040;
constexpr std::uint32_t FIO2PIN = 0x2009C054;

}  // namespace

TEST(KeilFirmware, LedInitializeConfiguresTheLedPins) {
    Board board;
    FirmwareBinding bind(board);
    EXPECT_EQ(LED_Initialize(), 0);
    EXPECT_EQ(board.mcu().read32(FIO1DIR), 0xB0000000u) << "P1.28, P1.29, P1.31 outputs";
    EXPECT_EQ(board.mcu().read32(FIO2DIR), 0x0000007Cu) << "P2.2-P2.6 outputs";
    for (unsigned i = 0; i < kLedCount; ++i) EXPECT_EQ(board.led(i), LedState::Off) << "LED" << i;
    EXPECT_EQ(board.mcu().read32(FIO1PIN), 0x4FFFC713u);
    EXPECT_EQ(board.mcu().read32(FIO2PIN), 0x00003F83u);
}

TEST(KeilFirmware, LedOnOffAndSetOutChangeTheBoardLeds) {
    Board board;
    FirmwareBinding bind(board);
    LED_Initialize();
    EXPECT_EQ(LED_GetCount(), kLedCount);
    EXPECT_EQ(LED_On(0), 0);
    EXPECT_EQ(board.led(0), LedState::On);
    // The LPC1768 simulator read these with LED0 on and LED1-7 off
    // (spikes/uvsim-script/e2-run1.out).
    EXPECT_EQ(board.mcu().read32(FIO1PIN), 0x5FFFC713u);
    EXPECT_EQ(board.mcu().read32(FIO2PIN), 0x00003F83u);
    EXPECT_EQ(LED_Off(0), 0);
    EXPECT_EQ(board.led(0), LedState::Off);
    EXPECT_EQ(LED_SetOut(0x81), 0);
    for (unsigned i = 0; i < kLedCount; ++i)
        EXPECT_EQ(board.led(i), (i == 0 || i == 7) ? LedState::On : LedState::Off) << "LED" << i;
    EXPECT_EQ(LED_On(kLedCount), -1) << "the driver's own range check";
}

TEST(KeilFirmware, ReachesTheSameStateAsThePhase1CppApi) {
    Board firmware;
    {
        FirmwareBinding bind(firmware);
        LED_Initialize();
        LED_On(0);
        LED_On(3);
        LED_Off(0);
        LED_SetOut(0x5A);
    }
    Board cpp;
    latasim::mcb1700::KeilBoardLed leds(cpp.mcu());
    leds.initialize();
    leds.on(0);
    leds.on(3);
    leds.off(0);
    leds.set_out(0x5A);
    EXPECT_EQ(snapshot(firmware), snapshot(cpp));
}

TEST(KeilFirmware, JoystickDriverReadsBoardPresses) {
    Board board;
    FirmwareBinding bind(board);
    EXPECT_EQ(Joystick_Initialize(), 0);
    EXPECT_EQ(Joystick_GetState(), 0u) << "all released";
    const struct {
        JoystickDirection direction;
        uint32_t keil_bit;
    } cases[] = {{JoystickDirection::Center, JOYSTICK_CENTER}, {JoystickDirection::Up, JOYSTICK_UP},
                 {JoystickDirection::Right, JOYSTICK_RIGHT},   {JoystickDirection::Down, JOYSTICK_DOWN},
                 {JoystickDirection::Left, JOYSTICK_LEFT}};
    for (const auto& c : cases) {
        board.press(c.direction);
        EXPECT_EQ(Joystick_GetState(), c.keil_bit) << to_string(c.direction);
        board.release(c.direction);
    }
    EXPECT_EQ(Joystick_GetState(), 0u);
}

TEST(KeilFirmware, ButtonsDriverReadsInt0) {
    Board board;
    FirmwareBinding bind(board);
    EXPECT_EQ(Buttons_Initialize(), 0);
    EXPECT_EQ(Buttons_GetCount(), 1u);
    EXPECT_EQ(Buttons_GetState(), 0u);
    board.press_int0();
    EXPECT_EQ(Buttons_GetState(), 1u) << "BUTTON_INT0 (bit 0)";
    board.release_int0();
    EXPECT_EQ(Buttons_GetState(), 0u);
}

// Input -> firmware -> output: the joystick state, as read by the Keil driver, is
// shown on the LEDs by the Keil LED driver.
TEST(KeilFirmware, JoystickStateShownOnLeds) {
    Board board;
    FirmwareBinding bind(board);
    LED_Initialize();
    Joystick_Initialize();
    board.press(JoystickDirection::Up);
    board.press(JoystickDirection::Left);
    LED_SetOut(Joystick_GetState());  // JOYSTICK_UP | JOYSTICK_LEFT = bits 3 and 0
    for (unsigned i = 0; i < kLedCount; ++i)
        EXPECT_EQ(board.led(i), (i == 0 || i == 3) ? LedState::On : LedState::Off) << "LED" << i;
}

TEST(KeilFirmware, RepeatedRunsReachIdenticalState) {
    auto run = [] {
        Board board;
        FirmwareBinding bind(board);
        LED_Initialize();
        Joystick_Initialize();
        Buttons_Initialize();
        board.press(JoystickDirection::Down);
        LED_SetOut(Joystick_GetState() | Buttons_GetState());
        LED_On(7);
        return snapshot(board);
    };
    EXPECT_EQ(run(), run());
}

// The Phase 2 end-to-end scenario: Keil's drivers initialise the board, light an
// LED, read an injected joystick press and INT0 press, and show both on the LEDs.
// Every step is checked in the hardware trace, and a repeat run matches exactly.
namespace {

using latasim::TraceEvent;
using latasim::TraceKind;

struct ScenarioResult {
    std::vector<TraceEvent> trace;
    latasim::test::GpioSnapshot state;
};

std::vector<TraceEvent> new_events(const Board& board, std::size_t& seen) {
    const auto& all = board.mcu().trace().events();
    std::vector<TraceEvent> fresh(all.begin() + static_cast<std::ptrdiff_t>(seen), all.end());
    seen = all.size();
    return fresh;
}

ScenarioResult run_scenario() {
    Board board;
    FirmwareBinding bind(board);
    std::size_t seen = 0;

    LED_Initialize();
    Joystick_Initialize();
    Buttons_Initialize();
    const auto init = new_events(board, seen);
    // GPIO_PortClock(1) first: Keil's PCONP read-modify-write.
    EXPECT_EQ(init.at(0).kind, TraceKind::Read);
    EXPECT_EQ(init.at(0).address, latasim::lpc17xx::kPconpAddress);
    EXPECT_EQ(init.at(1).kind, TraceKind::Write);
    for (unsigned i = 0; i < kLedCount; ++i) EXPECT_EQ(board.led(i), LedState::Off);

    LED_On(0);
    const auto led_on = new_events(board, seen);
    EXPECT_EQ(led_on.size(), 2u);
    EXPECT_EQ(led_on.at(0).kind, TraceKind::Write);
    EXPECT_EQ(led_on.at(0).address, FIO1PIN + 4) << "FIO1SET";
    EXPECT_EQ(led_on.at(0).value, 1u << 28);
    EXPECT_EQ(led_on.at(1).kind, TraceKind::Led);
    EXPECT_EQ(led_on.at(1).led, 0u);
    EXPECT_EQ(led_on.at(1).value, static_cast<std::uint32_t>(LedState::On));

    board.press(JoystickDirection::Up);
    const auto press = new_events(board, seen);
    EXPECT_EQ(press.size(), 1u);
    EXPECT_EQ(press.at(0).kind, TraceKind::Input);
    EXPECT_EQ(press.at(0).port, 1u);
    EXPECT_EQ(press.at(0).pin, 23u);
    EXPECT_EQ(press.at(0).value, 0u) << "pressed = low";

    const uint32_t joystick = Joystick_GetState();
    EXPECT_EQ(joystick, static_cast<uint32_t>(JOYSTICK_UP));
    const auto joystick_reads = new_events(board, seen);
    EXPECT_EQ(joystick_reads.size(), 5u) << "one FIO1PIN read per direction";
    for (const auto& e : joystick_reads) {
        EXPECT_EQ(e.kind, TraceKind::Read);
        EXPECT_EQ(e.address, FIO1PIN);
        EXPECT_EQ(e.value & (1u << 23), 0u) << "the firmware saw the press";
    }

    board.press_int0();
    const auto int0 = new_events(board, seen);
    EXPECT_EQ(int0.size(), 1u);
    EXPECT_EQ(int0.at(0).port, 2u);
    EXPECT_EQ(int0.at(0).pin, 10u);
    const uint32_t buttons = Buttons_GetState();
    EXPECT_EQ(buttons, 1u);
    const auto button_read = new_events(board, seen);
    EXPECT_EQ(button_read.size(), 1u);
    EXPECT_EQ(button_read.at(0).address, FIO2PIN);
    EXPECT_EQ(button_read.at(0).value & (1u << 10), 0u);

    LED_SetOut(joystick | buttons);  // LED3 for UP, LED0 for INT0
    board.release(JoystickDirection::Up);
    board.release_int0();
    for (unsigned i = 0; i < kLedCount; ++i)
        EXPECT_EQ(board.led(i), (i == 0 || i == 3) ? LedState::On : LedState::Off) << "LED" << i;
    EXPECT_EQ(board.mcu().read32(FIO1PIN) & (1u << 23), 1u << 23) << "released";

    return {board.mcu().trace().events(), snapshot(board)};
}

}  // namespace

TEST(KeilFirmware, EndToEndScenarioIsTracedAndDeterministic) {
    const ScenarioResult first = run_scenario();
    const ScenarioResult second = run_scenario();
    EXPECT_EQ(first.trace, second.trace);
    EXPECT_EQ(first.state, second.state);
    for (std::size_t i = 0; i < first.trace.size(); ++i) EXPECT_EQ(first.trace[i].seq, i + 1);
}
