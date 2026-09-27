// B1 (board API) and B2 (direct register access) are two ways into one model.
// Equivalent operations must leave identical GPIO state, and each path must see
// the other's writes.
#include "boards/mcb1700/board.hpp"
#include "boards/mcb1700/keil_board_led.hpp"
#include "lpc17xx/keil_gpio_driver.hpp"

#include <gtest/gtest.h>

#include <array>

using latasim::lpc17xx::bit_band_alias;
using latasim::lpc17xx::Gpio;
using latasim::lpc17xx::gpio_register_address;
using latasim::lpc17xx::GpioReg;
using latasim::lpc17xx::KeilGpioDriver;
using latasim::lpc17xx::Lpc1768;
using latasim::mcb1700::Board;
using latasim::mcb1700::KeilBoardLed;
using latasim::mcb1700::kLedCount;
using latasim::mcb1700::LedState;

namespace {

constexpr std::uint32_t FIO1DIR = 0x2009C020;
constexpr std::uint32_t FIO1PIN = 0x2009C034;
constexpr std::uint32_t FIO1SET = 0x2009C038;
constexpr std::uint32_t FIO1CLR = 0x2009C03C;
constexpr std::uint32_t FIO2DIR = 0x2009C040;
constexpr std::uint32_t FIO2SET = 0x2009C058;
constexpr std::uint32_t FIO2CLR = 0x2009C05C;

// Everything observable about GPIO: all readable registers of every port, plus
// what the board shows on its LEDs.
struct Snapshot {
    std::array<std::array<std::uint32_t, 4>, Gpio::kPortCount> regs{};
    std::array<LedState, kLedCount> leds{};
    bool operator==(const Snapshot&) const = default;
};

Snapshot snapshot(const Board& board) {
    Snapshot s;
    for (unsigned p = 0; p < Gpio::kPortCount; ++p) {
        const Gpio& g = board.mcu().gpio();
        s.regs[p] = {g.read(p, GpioReg::Dir), g.read(p, GpioReg::Mask), g.read(p, GpioReg::Pin),
                     g.read(p, GpioReg::Set)};
    }
    for (unsigned i = 0; i < kLedCount; ++i) s.leds[i] = board.led(i);
    return s;
}

void set_bits(Lpc1768& mcu, std::uint32_t address, std::uint32_t bits) {
    mcu.write32(address, mcu.read32(address) | bits);
}

}  // namespace

TEST(Convergence, BoardApiAndRegisterWritesReachIdenticalState) {
    Board api;
    KeilBoardLed leds(api.mcu());
    leds.initialize();
    leds.on(0);
    leds.on(3);

    Board regs;  // what LED_Initialize + LED_On(0) + LED_On(3) do, as firmware stores
    set_bits(regs.mcu(), FIO1DIR, (1u << 28) | (1u << 29) | (1u << 31));
    set_bits(regs.mcu(), FIO2DIR, 0x7Cu);
    regs.mcu().write32(FIO1CLR, (1u << 28) | (1u << 29) | (1u << 31));
    regs.mcu().write32(FIO2CLR, 0x7Cu);
    regs.mcu().write32(FIO1SET, 1u << 28);
    regs.mcu().write32(FIO2SET, 1u << 2);

    EXPECT_EQ(snapshot(api), snapshot(regs));
    EXPECT_EQ(api.led(0), LedState::On);
    EXPECT_EQ(api.led(3), LedState::On);
}

TEST(Convergence, BitBandAliasAndBoardApiReachIdenticalState) {
    Board api;
    KeilBoardLed leds(api.mcu());
    leds.initialize();
    leds.on(0);

    Board alias;
    KeilBoardLed(alias.mcu()).initialize();
    alias.mcu().write32(bit_band_alias(FIO1SET, 28), 1);

    EXPECT_EQ(snapshot(api), snapshot(alias));
}

TEST(Convergence, RegisterWriteTurnsOffAnLedTheApiTurnedOn) {
    Board board;
    KeilBoardLed leds(board.mcu());
    leds.initialize();
    leds.on(0);
    board.mcu().write32(FIO1CLR, 1u << 28);
    EXPECT_EQ(board.led(0), LedState::Off);
    EXPECT_EQ(KeilGpioDriver(board.mcu()).pin_read(1, 28), 0u) << "the API reads the register write back";
}

TEST(Convergence, ApiSeesAnLedTheRegisterPathTurnedOn) {
    Board board;
    board.mcu().write32(FIO1DIR, 1u << 28);
    board.mcu().write32(bit_band_alias(FIO1PIN, 28), 1);
    EXPECT_EQ(board.led(0), LedState::On);
    EXPECT_EQ(KeilGpioDriver(board.mcu()).pin_read(1, 28), 1u);
    KeilBoardLed(board.mcu()).off(0);
    EXPECT_EQ(board.mcu().read32(FIO1PIN) & (1u << 28), 0u);
}

// The wrong-pin alias from Phase 0 must not undo what the API did to LED0.
TEST(Convergence, WrongPinAliasDoesNotChangeTheApiControlledLed) {
    Board board;
    KeilBoardLed leds(board.mcu());
    leds.initialize();
    leds.on(0);
    board.mcu().write32(0x233806EC, 0);  // P1.27's alias, meant for P1.28
    EXPECT_EQ(board.led(0), LedState::On);
    board.mcu().write32(bit_band_alias(FIO1PIN, 28), 0);  // the correct alias
    EXPECT_EQ(board.led(0), LedState::Off);
}

TEST(Convergence, MaskedPinIgnoresTheApiToo) {
    Board board;
    KeilBoardLed leds(board.mcu());
    leds.initialize();
    board.mcu().write32(gpio_register_address(1, GpioReg::Mask), 1u << 28);
    leds.on(0);  // FIOSET write is masked
    EXPECT_EQ(board.led(0), LedState::Off);
}
