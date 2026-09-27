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

#include <gtest/gtest.h>

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
