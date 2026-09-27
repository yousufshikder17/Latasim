// Keil's own register-level GPIO driver (RTE_Driver/GPIO_LPC17xx.c in LPC1700_DFP),
// compiled unchanged as C++ against the host LPC17xx.h. Its register expressions,
// such as LPC_GPIO(port_num)->DIR |= (1UL << pin_num), reach the model through the
// register proxies. This executable does not link Latasim's own GPIO_* functions.
#include "GPIO_LPC17xx.h"

#include "gpio_snapshot.hpp"
#include "host/binding.hpp"
#include "lpc17xx/keil_gpio_driver.hpp"

#include <gtest/gtest.h>

using latasim::host::FirmwareBinding;
using latasim::lpc17xx::KeilGpioDriver;
using latasim::mcb1700::Board;
using latasim::mcb1700::JoystickDirection;
using latasim::mcb1700::LedState;
using latasim::test::snapshot;

TEST(KeilRegisterDriver, DrivesLedsThroughRegisterExpressions) {
    Board board;
    FirmwareBinding bind(board);
    GPIO_SetDir(2, 6, GPIO_DIR_OUTPUT);  // LED7
    GPIO_PinWrite(2, 6, 1);
    EXPECT_EQ(board.led(7), LedState::On);
    EXPECT_EQ(GPIO_PinRead(2, 6), 1u);
    GPIO_PinWrite(2, 6, 0);
    EXPECT_EQ(board.led(7), LedState::Off);
}

TEST(KeilRegisterDriver, ReadsBoardInputs) {
    Board board;
    FirmwareBinding bind(board);
    GPIO_SetDir(2, 10, GPIO_DIR_INPUT);  // INT0
    EXPECT_EQ(GPIO_PinRead(2, 10), 1u);
    board.press_int0();
    EXPECT_EQ(GPIO_PinRead(2, 10), 0u);
    board.press(JoystickDirection::Left);
    EXPECT_EQ(GPIO_PortRead(1) & (1u << 26), 0u);
}

TEST(KeilRegisterDriver, PortWriteUsesTheMask) {
    Board board;
    FirmwareBinding bind(board);
    for (unsigned pin = 2; pin <= 6; ++pin) GPIO_SetDir(2, pin, GPIO_DIR_OUTPUT);
    GPIO_PortWrite(2, 0x14, 0xFFFFFFFF);  // only P2.2 and P2.4 (LED3, LED5)
    for (unsigned i = 3; i <= 7; ++i)
        EXPECT_EQ(board.led(i), (i == 3 || i == 5) ? LedState::On : LedState::Off) << "LED" << i;
}

TEST(KeilRegisterDriver, MatchesThePhase1DriverEmulation) {
    Board keil;
    {
        FirmwareBinding bind(keil);
        GPIO_SetDir(1, 28, GPIO_DIR_OUTPUT);
        GPIO_SetDir(2, 2, GPIO_DIR_OUTPUT);
        GPIO_PinWrite(1, 28, 1);
        GPIO_PinWrite(2, 2, 1);
        GPIO_PinWrite(1, 28, 0);
        GPIO_SetDir(1, 23, GPIO_DIR_INPUT);
    }
    Board cpp;
    KeilGpioDriver gpio(cpp.mcu());
    gpio.set_dir(1, 28, true);
    gpio.set_dir(2, 2, true);
    gpio.pin_write(1, 28, 1);
    gpio.pin_write(2, 2, 1);
    gpio.pin_write(1, 28, 0);
    gpio.set_dir(1, 23, false);
    EXPECT_EQ(snapshot(keil), snapshot(cpp));
}

// GPIO_PortClock's LPC_SC->PCONP read-modify-write. PCONP was unmapped and this
// call faulted until the open-questions review (docs/phase2/open-questions.md).
TEST(KeilRegisterDriver, PortClockMatchesThePhase1DriverEmulation) {
    Board keil;
    {
        FirmwareBinding bind(keil);
        GPIO_PortClock(0);
        EXPECT_EQ(keil.mcu().pconp() & latasim::lpc17xx::kPconpGpio, 0u);
        GPIO_PortClock(1);
        GPIO_PortClock(0);
    }
    Board cpp;
    KeilGpioDriver gpio(cpp.mcu());
    gpio.port_clock(false);
    gpio.port_clock(true);
    gpio.port_clock(false);
    EXPECT_EQ(snapshot(keil), snapshot(cpp));
}
