// MCB1700 joystick and INT0 button, observed the way firmware reads them.
#include "boards/mcb1700/board.hpp"
#include "lpc17xx/keil_gpio_driver.hpp"

#include <gtest/gtest.h>

using latasim::lpc17xx::KeilGpioDriver;
using latasim::mcb1700::Board;
using latasim::mcb1700::JoystickDirection;
using latasim::mcb1700::kInt0Pin;
using latasim::mcb1700::kJoystickDirectionCount;
using latasim::mcb1700::kJoystickPins;

namespace {

constexpr std::uint32_t FIO1DIR = 0x2009C020;
constexpr std::uint32_t FIO1MASK = 0x2009C030;
constexpr std::uint32_t FIO1PIN = 0x2009C034;
constexpr std::uint32_t FIO1SET = 0x2009C038;
constexpr std::uint32_t FIO2PIN = 0x2009C054;

constexpr JoystickDirection kAll[] = {JoystickDirection::Center, JoystickDirection::Up, JoystickDirection::Right,
                                      JoystickDirection::Down, JoystickDirection::Left};

// Legacy joystick driver (KBD.c): get_button() = ~(FIO1PIN >> 20) & KBD_MASK.
std::uint32_t legacy_get_button(const Board& board) {
    return ~(board.mcu().read32(FIO1PIN) >> 20) & 0x79u;
}

// Legacy KBD.h codes, in JoystickDirection order (pin map, "Legacy bit" column).
constexpr std::uint32_t kLegacyCode[] = {0x01, 0x08, 0x10, 0x20, 0x40};

}  // namespace

TEST(BoardInputs, PinsMatchThePhase0PinMap) {
    const unsigned expected[kJoystickDirectionCount] = {20, 23, 24, 25, 26};  // center, up, right, down, left
    for (unsigned i = 0; i < kJoystickDirectionCount; ++i) {
        EXPECT_EQ(kJoystickPins[i].port, 1u);
        EXPECT_EQ(kJoystickPins[i].pin, expected[i]);
    }
    EXPECT_EQ(kInt0Pin.port, 2u);
    EXPECT_EQ(kInt0Pin.pin, 10u);
}

TEST(BoardInputs, EverythingStartsReleasedAndReadsHigh) {
    const Board board;
    for (const auto d : kAll) EXPECT_FALSE(board.is_pressed(d));
    EXPECT_FALSE(board.int0_pressed());
    EXPECT_EQ(board.mcu().read32(FIO1PIN) & 0x07900000u, 0x07900000u) << "P1.20, P1.23-P1.26 high";
    EXPECT_TRUE(board.mcu().gpio().pin_level(2, 10));
    EXPECT_EQ(legacy_get_button(board), 0u);
}

TEST(BoardInputs, PressDrivesTheJoystickPinLowAndReleaseDrivesItHigh) {
    Board board;
    board.press(JoystickDirection::Up);
    EXPECT_TRUE(board.is_pressed(JoystickDirection::Up));
    EXPECT_FALSE(board.mcu().gpio().pin_level(1, 23));
    board.release(JoystickDirection::Up);
    EXPECT_FALSE(board.is_pressed(JoystickDirection::Up));
    EXPECT_TRUE(board.mcu().gpio().pin_level(1, 23));
}

TEST(BoardInputs, EachDirectionAffectsOnlyItsOwnPin) {
    for (unsigned i = 0; i < kJoystickDirectionCount; ++i) {
        Board board;
        board.press(kAll[i]);
        for (unsigned j = 0; j < kJoystickDirectionCount; ++j)
            EXPECT_EQ(board.mcu().gpio().pin_level(1, kJoystickPins[j].pin), i != j)
                << to_string(kAll[i]) << " pressed, checking " << to_string(kAll[j]);
        EXPECT_TRUE(board.mcu().gpio().pin_level(2, 10)) << "INT0 unaffected";
    }
}

TEST(BoardInputs, LegacyDriverDecodesEachDirection) {
    for (unsigned i = 0; i < kJoystickDirectionCount; ++i) {
        Board board;
        board.press(kAll[i]);
        EXPECT_EQ(legacy_get_button(board), kLegacyCode[i]) << to_string(kAll[i]);
    }
}

TEST(BoardInputs, KeilGpioDriverReadsPressedAsZero) {  // Joystick_GetState: !GPIO_PinRead(...)
    Board board;
    KeilGpioDriver gpio(board.mcu());
    EXPECT_EQ(gpio.pin_read(1, 25), 1u);
    board.press(JoystickDirection::Down);
    EXPECT_EQ(gpio.pin_read(1, 25), 0u);
}

TEST(BoardInputs, Int0IsActiveLowOnP2_10) {
    Board board;
    board.press_int0();
    EXPECT_TRUE(board.int0_pressed());
    EXPECT_EQ(board.mcu().read32(FIO2PIN) & (1u << 10), 0u);
    EXPECT_EQ(board.mcu().read8(FIO2PIN + 1) & (1u << 2), 0u) << "also through the FIO2PIN1 byte view";
    board.release_int0();
    EXPECT_FALSE(board.int0_pressed());
    EXPECT_EQ(board.mcu().read32(FIO2PIN) & (1u << 10), 1u << 10);
}

TEST(BoardInputs, PressingNeverTouchesTheOutputLatch) {
    Board board;
    board.mcu().write32(FIO1SET, 1u << 23);  // firmware latched P1.23 high while it is an input
    board.press(JoystickDirection::Up);
    EXPECT_EQ(board.mcu().read32(FIO1SET), 1u << 23);
    EXPECT_FALSE(board.mcu().gpio().pin_level(1, 23));
}

TEST(BoardInputs, AnOutputPinShowsItsLatchNotTheJoystick) {
    Board board;
    board.press(JoystickDirection::Left);
    board.mcu().write32(FIO1DIR, 1u << 26);  // firmware (wrongly) drives P1.26
    board.mcu().write32(FIO1SET, 1u << 26);
    EXPECT_TRUE(board.mcu().gpio().pin_level(1, 26));
    EXPECT_TRUE(board.is_pressed(JoystickDirection::Left)) << "the physical switch is still held";
    board.mcu().write32(FIO1DIR, 0);  // back to input: the held switch shows again
    EXPECT_FALSE(board.mcu().gpio().pin_level(1, 26));
}

TEST(BoardInputs, MaskedJoystickPinReadsZeroWhateverItsState) {
    Board board;
    board.mcu().write32(FIO1MASK, 1u << 20);
    EXPECT_EQ(board.mcu().read32(FIO1PIN) & (1u << 20), 0u) << "released but masked";
    board.press(JoystickDirection::Center);
    EXPECT_EQ(board.mcu().read32(FIO1PIN) & (1u << 20), 0u);
    board.release(JoystickDirection::Center);
    board.mcu().write32(FIO1MASK, 0);
    EXPECT_EQ(board.mcu().read32(FIO1PIN) & (1u << 20), 1u << 20);
}

TEST(BoardInputs, SeveralSwitchesCanBeHeldAtOnce) {
    Board board;
    board.press(JoystickDirection::Center);
    board.press(JoystickDirection::Up);
    board.press_int0();
    EXPECT_EQ(legacy_get_button(board), 0x01u | 0x08u);
    EXPECT_FALSE(board.mcu().gpio().pin_level(2, 10));
}
