// The C boundary: C code calls Keil-named GPIO functions that reach the bound
// board's authoritative model.
#include "boards/mcb1700/keil_board_led.hpp"
#include "gpio_snapshot.hpp"
#include "host/binding.hpp"
#include "host/gpio_client.h"
#include "lpc17xx/keil_gpio_driver.hpp"

#include <gtest/gtest.h>

#include <stdexcept>

using latasim::host::bound_board;
using latasim::host::FirmwareBinding;
using latasim::lpc17xx::KeilGpioDriver;
using latasim::mcb1700::Board;
using latasim::mcb1700::JoystickDirection;
using latasim::mcb1700::LedState;
using latasim::test::snapshot;

TEST(HostCApi, ClientIsCompiledAsC) {
    EXPECT_EQ(c_char_literal_size(), static_cast<int>(sizeof(int)));
}

TEST(HostCApi, CGpioCallsDriveTheBoundBoardsLed) {
    Board board;
    FirmwareBinding bind(board);
    EXPECT_EQ(board.led(0), LedState::Undriven);
    c_led_pin_init(1, 28);
    EXPECT_EQ(board.led(0), LedState::Off);
    c_pin_write(1, 28, 1);
    EXPECT_EQ(board.led(0), LedState::On);
    EXPECT_EQ(board.mcu().read32(0x2009C038) & (1u << 28), 1u << 28) << "the FIOSET latch holds it";
    c_pin_write(1, 28, 0);
    EXPECT_EQ(board.led(0), LedState::Off);
}

TEST(HostCApi, CPinReadSeesBoardInputs) {
    Board board;
    FirmwareBinding bind(board);
    c_input_pin_init(1, 23);  // joystick up
    EXPECT_EQ(c_pin_read(1, 23), 1u);
    board.press(JoystickDirection::Up);
    EXPECT_EQ(c_pin_read(1, 23), 0u);
    board.release(JoystickDirection::Up);
    EXPECT_EQ(c_pin_read(1, 23), 1u);
}

TEST(HostCApi, CAndCppPathsReachIdenticalState) {
    Board via_c;
    {
        FirmwareBinding bind(via_c);
        c_led_pin_init(1, 28);
        c_led_pin_init(2, 2);
        c_pin_write(1, 28, 1);
        c_pin_write(2, 2, 1);
        c_pin_write(1, 28, 0);
    }
    Board via_cpp;
    KeilGpioDriver gpio(via_cpp.mcu());
    for (const auto [port, pin] : {std::pair{1u, 28u}, std::pair{2u, 2u}}) {
        gpio.set_dir(port, pin, true);
        gpio.pin_write(port, pin, 0);
    }
    gpio.pin_write(1, 28, 1);
    gpio.pin_write(2, 2, 1);
    gpio.pin_write(1, 28, 0);
    EXPECT_EQ(snapshot(via_c), snapshot(via_cpp));
}

TEST(HostCApi, CAndKeilBoardLedApiReachIdenticalState) {
    Board via_c;
    {
        FirmwareBinding bind(via_c);
        for (const auto& p : latasim::mcb1700::kLedPins) c_led_pin_init(p.port, p.pin);
        c_pin_write(2, 4, 1);  // LED5
    }
    Board via_api;
    latasim::mcb1700::KeilBoardLed leds(via_api.mcu());
    leds.initialize();
    leds.on(5);
    EXPECT_EQ(snapshot(via_c), snapshot(via_api));
}

TEST(HostCApi, BindingIsScopedAndExclusive) {
    Board a, b;
    EXPECT_EQ(bound_board(), nullptr);
    {
        FirmwareBinding bind(a);
        EXPECT_EQ(bound_board(), &a);
        EXPECT_THROW(FirmwareBinding second(b), std::logic_error);
        EXPECT_EQ(bound_board(), &a) << "a failed second binding leaves the first in place";
    }
    EXPECT_EQ(bound_board(), nullptr);
    FirmwareBinding rebind(b);
    EXPECT_EQ(bound_board(), &b);
}

TEST(HostCApiDeathTest, CallWithoutABoundBoardAborts) {
    ASSERT_EQ(bound_board(), nullptr);
    EXPECT_DEATH(c_pin_write(1, 28, 1), "GPIO_PinWrite: no board bound");
}

TEST(HostCApiDeathTest, InvalidPinFromCAborts) {
    Board board;
    FirmwareBinding bind(board);
    EXPECT_DEATH(c_pin_read(1, 32), "GPIO_PinRead: GPIO pin 32 out of range");
    EXPECT_DEATH(c_pin_write(5, 0, 1), "GPIO_PinWrite: bus fault");
}

// The C layer adds no accesses of its own: firmware calls trace exactly the
// register traffic of the Phase 1 C++ driver emulation.
TEST(HostCApi, CAndCppPathsRecordIdenticalTraces) {
    Board via_c;
    {
        FirmwareBinding bind(via_c);
        c_led_pin_init(1, 28);
        c_pin_write(1, 28, 1);
        c_input_pin_init(1, 23);
        c_pin_read(1, 23);
    }
    Board via_cpp;
    KeilGpioDriver gpio(via_cpp.mcu());
    gpio.port_clock(true);
    gpio.set_dir(1, 28, true);
    gpio.pin_write(1, 28, 0);
    gpio.pin_write(1, 28, 1);
    gpio.port_clock(true);
    gpio.set_dir(1, 23, false);
    gpio.pin_read(1, 23);
    EXPECT_EQ(via_c.mcu().trace().events(), via_cpp.mcu().trace().events());
}
