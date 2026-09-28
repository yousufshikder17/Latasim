// Firmware-style register expressions (device/LPC17xx.h) reaching the model.
#include "LPC17xx.h"

#include "boards/mcb1700/keil_board_led.hpp"
#include "gpio_snapshot.hpp"
#include "host/binding.hpp"
#include "host/c/latasim_keil_gpio.h"
#include "host/gpio_client.h"
#include "lpc17xx/keil_gpio_driver.hpp"
#include "lpc17xx/lpc1768.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>

using latasim::host::FirmwareBinding;
using latasim::lpc17xx::bit_band_alias;
using latasim::lpc17xx::KeilGpioDriver;
using latasim::mcb1700::Board;
using latasim::mcb1700::JoystickDirection;
using latasim::mcb1700::LedState;
using latasim::test::snapshot;

namespace {

constexpr std::uint32_t FIO1DIR = 0x2009C020;
constexpr std::uint32_t FIO1PIN = 0x2009C034;
constexpr std::uint32_t FIO1SET = 0x2009C038;
constexpr std::uint32_t FIO1CLR = 0x2009C03C;
constexpr std::uint32_t FIO2DIR = 0x2009C040;

template <typename T>
bool all_zero(const T& object) {
    const auto* bytes = reinterpret_cast<const unsigned char*>(&object);
    return std::all_of(bytes, bytes + sizeof object, [](unsigned char b) { return b == 0; });
}

}  // namespace

TEST(HostRegisters, StructRegisterWritesReachTheBoard) {
    Board board;
    FirmwareBinding bind(board);
    LPC_GPIO1->FIODIR |= 1UL << 28;
    LPC_GPIO1->FIOCLR = 1UL << 28;
    EXPECT_EQ(board.led(0), LedState::Off);
    LPC_GPIO1->FIOSET = 1UL << 28;
    EXPECT_EQ(board.led(0), LedState::On);
    EXPECT_EQ(board.mcu().read32(FIO1DIR), 1u << 28);
    LPC_GPIO1->FIODIR &= ~(1UL << 28);
    EXPECT_EQ(board.mcu().read32(FIO1DIR), 0u);
}

TEST(HostRegisters, StructRegisterReadsSeeTheBoard) {
    Board board;
    FirmwareBinding bind(board);
    EXPECT_EQ(LPC_GPIO1->FIOPIN & (1UL << 23), 1UL << 23) << "joystick up released";
    board.press(JoystickDirection::Up);
    EXPECT_EQ(LPC_GPIO1->FIOPIN & (1UL << 23), 0UL);
    EXPECT_EQ(static_cast<uint32_t>(LPC_GPIO1->FIOPIN), board.mcu().read32(FIO1PIN));
    LPC_GPIO2->FIODIR = 0x7C;
    LPC_GPIO2->FIOSET = 0x04;
    EXPECT_EQ(LPC_GPIO2->FIOSET, 0x04u) << "FIOSET reads the latch";
    EXPECT_EQ(LPC_GPIO2->FIOCLR, 0u) << "FIOCLR reads 0";
}

TEST(HostRegisters, ByteAndHalfwordViewsUseNarrowAccesses) {
    Board board;
    FirmwareBinding bind(board);
    LPC_GPIO2->FIODIR0 = 0x7C;  // LED4..LED7's port-2 pins, one byte store
    EXPECT_EQ(board.mcu().read32(FIO2DIR), 0x7Cu);
    LPC_GPIO1->FIODIR3 = 0xB0;  // P1.28, P1.29, P1.31
    LPC_GPIO1->FIOSET3 = 0x10;  // byte 3 bit 4 = P1.28
    EXPECT_EQ(board.led(0), LedState::On);
    EXPECT_EQ(board.led(1), LedState::Off);
    EXPECT_EQ(LPC_GPIO1->FIOPIN3, board.mcu().read8(FIO1PIN + 3));
    EXPECT_EQ(LPC_GPIO1->FIOPINH, board.mcu().read16(FIO1PIN + 2));
    LPC_GPIO1->FIOCLRH = 0x1000;  // halfword 1 bit 12 = P1.28
    EXPECT_EQ(board.led(0), LedState::Off);
    EXPECT_EQ(board.mcu().read32(FIO1DIR), 0xB0000000u) << "lane stores left the other lanes alone";
}

TEST(HostRegisters, RegisterCAndCppPathsReachIdenticalState) {
    Board via_registers;
    {
        FirmwareBinding bind(via_registers);
        LPC_GPIO1->FIODIR |= 1UL << 28;
        LPC_GPIO1->FIOCLR = 1UL << 28;
        LPC_GPIO2->FIODIR |= 1UL << 2;
        LPC_GPIO2->FIOCLR = 1UL << 2;
        LPC_GPIO1->FIOSET = 1UL << 28;
        LPC_GPIO2->FIOSET = 1UL << 2;
        LPC_GPIO1->FIOCLR = 1UL << 28;
    }
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
    EXPECT_EQ(snapshot(via_registers), snapshot(via_c));
    EXPECT_EQ(snapshot(via_registers), snapshot(via_cpp));
}

TEST(HostRegisters, LiteralAddressesThroughTheReg32Macro) {
    Board board;
    FirmwareBinding bind(board);
    // Adapted from *(volatile uint32_t *)0x2009C020 |= ...; etc.
    LATASIM_REG32(0x2009C020) |= 1u << 28;
    LATASIM_REG32(0x2009C038) = 1u << 28;
    EXPECT_EQ(board.led(0), LedState::On);
    EXPECT_EQ(LATASIM_REG32(FIO1SET), 1u << 28);
    // A bit-band alias of FIO1CLR bit 28, as bit-band firmware computes it.
    LATASIM_REG32(bit_band_alias(FIO1CLR, 28)) = 1;
    EXPECT_EQ(board.led(0), LedState::Off);
    // Register-to-register assignment copies the value.
    LATASIM_REG32(FIO2DIR) = LATASIM_REG32(FIO1DIR);
    EXPECT_EQ(board.mcu().read32(FIO2DIR), 1u << 28);
}

// Bit-band firmware computes an alias from a register's address and keeps it in a
// pointer variable. The textbook macro works unchanged once its dereference is
// LATASIM_REG32 and the pointer is declared LATASIM_REG32_PTR.
#define BIT_BAND(reg, bit)                                                              \
    LATASIM_REG32(((unsigned long)(reg) & 0xF0000000) | 0x02000000 |                    \
                  (((unsigned long)(reg) & 0x000FFFFF) << 5) | ((bit) << 2))

TEST(HostRegisters, RegisterAddressesSupportBitBandArithmetic) {
    Board board;
    FirmwareBinding bind(board);
    EXPECT_EQ(static_cast<uint32_t>(&LPC_GPIO1->FIOPIN), FIO1PIN);
    EXPECT_EQ((unsigned long)(&LPC_GPIO1->FIOPIN3), FIO1PIN + 3) << "byte views have their own address";
    EXPECT_EQ(static_cast<uint32_t>(&LATASIM_REG32(FIO1SET)), FIO1SET);
    EXPECT_EQ(static_cast<uint32_t>(&LPC_SSP1->DR), 0x40030008u);

    LPC_GPIO1->FIODIR |= 1UL << 28;
    LATASIM_REG32_PTR bit = &BIT_BAND(&LPC_GPIO1->FIOPIN, 28);
    EXPECT_EQ(static_cast<uint32_t>(bit), bit_band_alias(FIO1PIN, 28));
    *bit = 1;
    EXPECT_EQ(board.led(0), LedState::On);
    *bit = 0;
    EXPECT_EQ(board.led(0), LedState::Off);
    EXPECT_EQ(static_cast<uint32_t>(*bit), 0u);
    bit = &LPC_GPIO1->FIOSET;  // a pointer to an ordinary register
    *bit = 1UL << 28;
    EXPECT_EQ(board.led(0), LedState::On);
    EXPECT_TRUE(all_zero(latasim_gpio_ports)) << "still nothing stored in host memory";
}

#undef BIT_BAND

TEST(HostRegisters, CFirmwareUsesTheMmioAccessors) {
    Board via_c;
    {
        FirmwareBinding bind(via_c);
        c_literal_led0_on();
    }
    EXPECT_EQ(via_c.led(0), LedState::On);
    Board via_registers;
    FirmwareBinding bind(via_registers);
    LPC_GPIO1->FIODIR |= 1UL << 28;
    LPC_GPIO1->FIOSET = 1UL << 28;
    EXPECT_EQ(snapshot(via_c), snapshot(via_registers));
}

TEST(HostRegisters, ProxiesHoldNoRegisterState) {
    Board board;
    FirmwareBinding bind(board);
    for (auto* port : {LPC_GPIO0, LPC_GPIO1, LPC_GPIO2, LPC_GPIO3, LPC_GPIO4}) {
        port->FIODIR = 0xFFFFFFFF;
        port->FIOMASK = 0x0000FFFF;
        port->FIOPIN = 0xA5A5A5A5;
        port->FIOSET0 = 0xFF;
        port->FIOCLRH = 0xFFFF;
    }
    EXPECT_NE(board.mcu().read32(FIO1DIR), 0u) << "the writes reached the model";
    EXPECT_TRUE(all_zero(latasim_gpio_ports)) << "and nothing was stored in host memory";
    EXPECT_TRUE(all_zero(latasim_sc));
}

// A register expression makes the same traced accesses as the driver call that
// Keil implements with it: GPIO_SetDir is FIODIR |= bit, GPIO_PinWrite is FIOSET = bit.
TEST(HostRegisters, RegisterAndCPathsRecordIdenticalTraces) {
    Board via_registers;
    {
        FirmwareBinding bind(via_registers);
        LPC_GPIO1->FIODIR |= 1UL << 28;
        LPC_GPIO1->FIOSET = 1UL << 28;
    }
    Board via_c;
    {
        FirmwareBinding bind(via_c);
        GPIO_SetDir(1, 28, GPIO_DIR_OUTPUT);
        GPIO_PinWrite(1, 28, 1);
    }
    EXPECT_EQ(via_registers.mcu().trace().events(), via_c.mcu().trace().events());
}

// PCONP was unmapped (an access faulted) until the open-questions review; it is now
// stored, so the register path and the C GPIO layer agree on GPIO_PortClock.
TEST(HostRegisters, PconpIsSharedWithTheCGpioLayer) {
    Board board;
    FirmwareBinding bind(board);
    EXPECT_EQ(LPC_SC->PCONP, latasim::lpc17xx::kPconpReset);
    GPIO_PortClock(0);
    EXPECT_EQ(LPC_SC->PCONP & (1UL << 15), 0UL);
    LPC_SC->PCONP |= 1UL << 15;
    EXPECT_EQ(board.mcu().pconp(), latasim::lpc17xx::kPconpReset);
}

TEST(HostRegistersDeathTest, UnmappedRegistersFault) {
    Board board;
    FirmwareBinding bind(board);
    EXPECT_DEATH(LATASIM_REG32(0x400FC0C0) = 0, "register write: bus fault: no register at 0x400FC0C0");  // PCON
    EXPECT_DEATH(LATASIM_REG32(0x2009C004) = 1, "register write: bus fault: no register at 0x2009C004");
    EXPECT_DEATH((void)static_cast<uint32_t>(LATASIM_REG32(0x10000000)),
                 "register read: bus fault: no register at 0x10000000");
    EXPECT_DEATH(latasim_mmio_write32(0x2009C036, 0), "register write: bus fault");  // misaligned
}

TEST(HostRegistersDeathTest, MisuseAborts) {
    EXPECT_DEATH(LPC_GPIO1->FIOSET = 1, "register write: no board bound");
    Board board;
    FirmwareBinding bind(board);
    EXPECT_DEATH(
        {
            LPC_GPIO_TypeDef local;
            local.FIOSET = 1;
        },
        "not a device register");
}

// Host CMSIS SysTick_Config: the register writes of core_cm3.h's version.
TEST(HostRegisters, SysTickConfigWritesReloadCurrentAndControl) {
    Board board;
    FirmwareBinding bind(board);
    EXPECT_EQ(SystemCoreClock, 100'000'000u);
    ASSERT_EQ(SysTick_Config(SystemCoreClock / 100), 0u);
    const auto& events = board.mcu().trace().events();
    ASSERT_EQ(events.size(), 4u);
    EXPECT_EQ(to_string(events[0]), "#1    t=0          write32 STRELOAD  0x000F423F");
    EXPECT_EQ(to_string(events[1]), "#2    t=0          write8  PRI_15    0xF8") << "NVIC_SetPriority(SysTick_IRQn, 31)";
    EXPECT_EQ(to_string(events[2]), "#3    t=0          write32 STCURR    0x00000000");
    EXPECT_EQ(to_string(events[3]), "#4    t=0          write32 STCTRL    0x00000007");
    EXPECT_EQ(board.mcu().nvic().priority(latasim::lpc17xx::kSysTickIrq), 0xF8u);
    EXPECT_EQ(SysTick_Config(0x01000001), 1u) << "reload value impossible";
}
