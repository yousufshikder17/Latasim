// The MCB1700 GLCD controller model (boards/mcb1700/glcd.hpp): its serial protocol,
// GRAM addressing and chip select on P0.6.
#include "boards/mcb1700/board.hpp"
#include "boards/mcb1700/glcd.hpp"
#include "trace/trace.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

using latasim::mcb1700::Board;
using latasim::mcb1700::Glcd;

namespace {

// One transaction as GLCD_MCB1700.c sends it: CS low, start byte, words, CS high.
void transaction(Glcd& glcd, std::uint8_t start, std::initializer_list<std::uint16_t> words) {
    glcd.chip_select(true);
    glcd.shift(start);
    for (std::uint16_t w : words) {
        glcd.shift(static_cast<std::uint8_t>(w >> 8));
        glcd.shift(static_cast<std::uint8_t>(w & 0xFF));
    }
    glcd.chip_select(false);
}
void wr_reg(Glcd& glcd, std::uint8_t reg, std::uint16_t value) {
    transaction(glcd, 0x70, {reg});    // index
    transaction(glcd, 0x72, {value});  // data
}
void write_pixels(Glcd& glcd, std::initializer_list<std::uint16_t> pixels) {
    transaction(glcd, 0x70, {0x22});
    transaction(glcd, 0x72, pixels);
}

}  // namespace

TEST(Glcd, ResetState) {
    Glcd glcd;
    EXPECT_EQ(glcd.reg(0x51), 239u) << "window: full GRAM";
    EXPECT_EQ(glcd.reg(0x53), 319u);
    for (unsigned x : {0u, 160u, 319u}) EXPECT_EQ(glcd.pixel(x, 0), 0u) << "black";
    Glcd other;
    EXPECT_EQ(glcd.hash(), other.hash());
}

TEST(Glcd, IndexThenDataWritesARegister) {
    Glcd glcd;
    wr_reg(glcd, 0x03, 0x1038);
    EXPECT_EQ(glcd.reg(0x03), 0x1038u);
    const auto writes = glcd.take_writes();
    ASSERT_EQ(writes.size(), 1u);
    EXPECT_EQ(writes[0].index, 0x03u);
    EXPECT_EQ(writes[0].value, 0x1038u);
}

// The driver's entry mode (0x1038: I/D = 11, AM = 1) fills a window vertical
// address first, i.e. across the screen in the driver's x.
TEST(Glcd, VerticalFirstFillWrapsInsideTheWindow) {
    Glcd glcd;
    wr_reg(glcd, 0x03, 0x1038);
    wr_reg(glcd, 0x50, 10);  // horizontal (screen y) 10..11
    wr_reg(glcd, 0x51, 11);
    wr_reg(glcd, 0x52, 20);  // vertical (screen x) 20..22
    wr_reg(glcd, 0x53, 22);
    wr_reg(glcd, 0x20, 10);
    wr_reg(glcd, 0x21, 20);
    write_pixels(glcd, {1, 2, 3, 4, 5, 6, 7});  // 7th wraps back to the start
    EXPECT_EQ(glcd.pixel(20, 10), 7u);
    EXPECT_EQ(glcd.pixel(21, 10), 2u);
    EXPECT_EQ(glcd.pixel(22, 10), 3u);
    EXPECT_EQ(glcd.pixel(20, 11), 4u);
    EXPECT_EQ(glcd.pixel(22, 11), 6u);
    EXPECT_EQ(glcd.pixel(23, 10), 0u) << "outside the window";
}

TEST(Glcd, HorizontalFirstAndDecrementingModes) {
    Glcd glcd;
    wr_reg(glcd, 0x03, 0x0000);  // AM = 0, I/D = 00: decrement both
    wr_reg(glcd, 0x50, 0);
    wr_reg(glcd, 0x51, 1);
    wr_reg(glcd, 0x52, 0);
    wr_reg(glcd, 0x53, 1);
    wr_reg(glcd, 0x20, 1);
    wr_reg(glcd, 0x21, 1);
    write_pixels(glcd, {1, 2, 3, 4});
    EXPECT_EQ(glcd.gram(1, 1), 1u);
    EXPECT_EQ(glcd.gram(0, 1), 2u) << "horizontal first, going down";
    EXPECT_EQ(glcd.gram(1, 0), 3u) << "then wrap to the window end and down one line";
    EXPECT_EQ(glcd.gram(0, 0), 4u);
}

TEST(Glcd, BoundaryPixels) {
    Glcd glcd;
    wr_reg(glcd, 0x03, 0x1038);
    for (const auto [x, y] : {std::pair{0u, 0u}, std::pair{319u, 0u}, std::pair{0u, 239u}, std::pair{319u, 239u}}) {
        wr_reg(glcd, 0x20, static_cast<std::uint16_t>(y));
        wr_reg(glcd, 0x21, static_cast<std::uint16_t>(x));
        write_pixels(glcd, {static_cast<std::uint16_t>(0xF000 | (x << 1) | y)});
        EXPECT_EQ(glcd.pixel(x, y), static_cast<std::uint16_t>(0xF000 | (x << 1) | y));
    }
}

TEST(Glcd, ReadsReturnTheIdAfterADummyByte) {
    Glcd glcd;
    transaction(glcd, 0x70, {0x00});
    glcd.chip_select(true);
    glcd.shift(0x73);  // RS = 1, RW = 1
    EXPECT_EQ(glcd.shift(0), 0u) << "dummy";
    EXPECT_EQ(glcd.shift(0), 0x93u);
    EXPECT_EQ(glcd.shift(0), 0x20u);
    glcd.chip_select(false);
}

TEST(Glcd, BytesCountOnlyWhileSelectedAndAfterAValidStartByte) {
    Glcd glcd;
    glcd.shift(0x70);  // not selected: ignored
    glcd.shift(0x00);
    glcd.shift(0x03);
    EXPECT_TRUE(glcd.take_writes().empty());
    transaction(glcd, 0x55, {0x0003});  // not 0x70 | RS | RW: the transaction is ignored
    EXPECT_EQ(glcd.bad_start_bytes(), 1u);
    transaction(glcd, 0x72, {0x1234});  // data for index 0 (the last index written)
    EXPECT_EQ(glcd.reg(0x00), 0x1234u);
}

TEST(Glcd, BurstsAreReportedWhenChipSelectEnds) {
    Glcd glcd;
    wr_reg(glcd, 0x03, 0x1038);
    glcd.take_writes();
    write_pixels(glcd, {1, 2, 3});
    const auto writes = glcd.take_writes();
    ASSERT_EQ(writes.size(), 1u);
    EXPECT_EQ(writes[0].index, 0x22u);
    EXPECT_EQ(writes[0].value, 3u) << "pixels";
}

// On the board, chip select is the P0.6 GPIO line, and the trace shows the
// controller's register writes.
TEST(Glcd, BoardWiresChipSelectToP06) {
    Board board;
    auto& mcu = board.mcu();
    mcu.write32(0x2009C000, 1u << 6);  // FIO0DIR: P0.6 output (latch 0: selected)
    board.glcd_transfer(0x70);
    board.glcd_transfer(0x00);
    board.glcd_transfer(0x07);
    mcu.write32(0x2009C018, 1u << 6);  // FIO0SET: deselect
    mcu.write32(0x2009C01C, 1u << 6);  // FIO0CLR: select
    for (std::uint8_t b : {std::uint8_t{0x72}, std::uint8_t{0x01}, std::uint8_t{0x37}}) board.glcd_transfer(b);
    mcu.write32(0x2009C018, 1u << 6);
    EXPECT_EQ(board.glcd().reg(0x07), 0x0137u);
    const auto& events = mcu.trace().events();
    EXPECT_EQ(to_string(events[events.size() - 2]).substr(6), "t=0          glcd    R07       0x0137")
        << "recorded as its last byte arrives, before chip select is released";
}

TEST(Glcd, ReleasedChipSelectIgnoresBytes) {
    Board board;  // P0.6 is an input reading high: not selected
    EXPECT_EQ(board.glcd_transfer(0x70), 0xFFu);
    EXPECT_TRUE(board.mcu().trace().events().empty());
}
