// Keil's GLCD_MCB1700.c and GLCD_Fonts.c (unchanged, from the pack) drawing on the
// modelled MCB1700 GLCD through the host Driver_SPI1.
extern "C" {  // Keil's header has no C++ guards; the driver is compiled as C
#include "Board_GLCD.h"
extern GLCD_FONT GLCD_Font_6x8;
extern GLCD_FONT GLCD_Font_16x24;
}

#include "boards/mcb1700/board.hpp"
#include "host/binding.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

using latasim::mcb1700::Board;
using latasim::mcb1700::Glcd;

namespace {

constexpr std::uint16_t kWhite = 0xFFFF;
constexpr std::uint16_t kBlue = 0x001F;
constexpr std::uint16_t kRed = 0xF800;

// A board with the display initialised and cleared to white.
struct Screen {
    Board board;
    latasim::host::FirmwareBinding bind{board};
    Screen() {
        GLCD_Initialize();
        GLCD_SetBackgroundColor(kWhite);
        GLCD_SetForegroundColor(kBlue);
        GLCD_ClearScreen();
    }
    const Glcd& glcd() const { return board.glcd(); }
    unsigned count(std::uint16_t colour) const {
        unsigned n = 0;
        for (unsigned y = 0; y < Glcd::kHeight; ++y)
            for (unsigned x = 0; x < Glcd::kWidth; ++x) n += glcd().pixel(x, y) == colour;
        return n;
    }
};

// What GLCD_DrawChar draws for `ch`, bit by bit from the font's own bitmap.
bool glyph_bit(const GLCD_FONT& font, int ch, unsigned col, unsigned row) {
    const unsigned bytes_per_row = (font.width + 7) / 8;
    const std::uint8_t* rows = font.bitmap + (static_cast<unsigned>(ch) - font.offset) * bytes_per_row * font.height;
    return (rows[row * bytes_per_row + col / 8] >> (col % 8)) & 1u;
}

}  // namespace

TEST(KeilGlcd, InitializeTakesTheIli9320Path) {
    Board board;
    latasim::host::FirmwareBinding bind(board);
    ASSERT_EQ(GLCD_Initialize(), 0);
    const Glcd& glcd = board.glcd();
    EXPECT_EQ(glcd.reg(0x03), 0x1038u) << "entry mode: I/D = 11, AM = 1 (GLCD_SWAP_XY)";
    EXPECT_EQ(glcd.reg(0x07), 0x0137u) << "262K colour, display on";
    EXPECT_EQ(glcd.reg(0x51), 239u);
    EXPECT_EQ(glcd.reg(0x53), 319u);
    EXPECT_EQ(glcd.reg(0x60), 0xA700u) << "gate scan with GLCD_MIRROR_Y (ILI9320 branch)";
    EXPECT_EQ(glcd.bad_start_bytes(), 0u);
    // "Turn LCD Backlight On" writes P4.28 without making it an output: the latch
    // is set, the pin is not driven (docs/phase4/open-questions.md).
    EXPECT_EQ(board.mcu().peek32(0x2009C098) & (1u << 28), 1u << 28) << "FIO4SET reads the latch";
    EXPECT_FALSE(board.mcu().gpio().is_output(4, 28));
}

TEST(KeilGlcd, ClearScreenFillsEveryPixel) {
    Screen screen;
    EXPECT_EQ(screen.count(kWhite), Glcd::kWidth * Glcd::kHeight);
    GLCD_SetBackgroundColor(kRed);
    GLCD_ClearScreen();
    EXPECT_EQ(screen.count(kRed), Glcd::kWidth * Glcd::kHeight);
    const auto& events = screen.board.mcu().trace().events();
    EXPECT_EQ(to_string(events[events.size() - 2]).substr(19), "write32 FIO0SET   0x00000040") << "CS released";
    EXPECT_EQ(to_string(events.back()).substr(19), "glcd    GRAM      76800 px") << "which ends the burst";
}

TEST(KeilGlcd, PixelsLandAtTheirCoordinates) {
    Screen screen;
    for (const auto [x, y] : {std::pair{0u, 0u}, std::pair{319u, 0u}, std::pair{0u, 239u}, std::pair{319u, 239u},
                              std::pair{160u, 120u}})
        GLCD_DrawPixel(x, y);
    EXPECT_EQ(screen.count(kBlue), 5u);
    EXPECT_EQ(screen.glcd().pixel(319, 239), kBlue);
    EXPECT_EQ(screen.glcd().pixel(160, 120), kBlue);
    EXPECT_EQ(screen.glcd().pixel(161, 120), kWhite);
}

TEST(KeilGlcd, RectangleDrawsItsEdges) {
    Screen screen;
    GLCD_DrawRectangle(10, 20, 30, 40);  // edges at x 10 and 40, y 20 and 60
    for (unsigned x = 10; x <= 39; ++x) {
        EXPECT_EQ(screen.glcd().pixel(x, 20), kBlue) << x;
        EXPECT_EQ(screen.glcd().pixel(x, 60), kBlue) << x;
    }
    for (unsigned y = 20; y <= 59; ++y) {
        EXPECT_EQ(screen.glcd().pixel(10, y), kBlue) << y;
        EXPECT_EQ(screen.glcd().pixel(40, y), kBlue) << y;
    }
    EXPECT_EQ(screen.glcd().pixel(25, 40), kWhite) << "inside";
}

TEST(KeilGlcd, CharactersMatchTheFontBitmap) {
    Screen screen;
    GLCD_SetFont(&GLCD_Font_16x24);
    GLCD_DrawChar(100, 50, 'A');
    const GLCD_FONT& font = GLCD_Font_16x24;
    unsigned ink = 0;
    for (unsigned row = 0; row < font.height; ++row)
        for (unsigned col = 0; col < font.width; ++col) {
            const bool on = glyph_bit(font, 'A', col, row);
            ink += on;
            EXPECT_EQ(screen.glcd().pixel(100 + col, 50 + row), on ? kBlue : kWhite) << col << "," << row;
        }
    EXPECT_GT(ink, 20u) << "'A' has ink";
}

TEST(KeilGlcd, StringsDrawEachCharacterInItsCell) {
    Screen screen;
    GLCD_SetFont(&GLCD_Font_6x8);
    const std::string text = "Latasim";
    GLCD_DrawString(0, 0, text.c_str());
    const GLCD_FONT& font = GLCD_Font_6x8;
    for (unsigned i = 0; i < text.size(); ++i)
        for (unsigned row = 0; row < font.height; ++row)
            for (unsigned col = 0; col < font.width; ++col)
                EXPECT_EQ(screen.glcd().pixel(i * font.width + col, row),
                          glyph_bit(font, text[i], col, row) ? kBlue : kWhite)
                    << text[i] << " " << col << "," << row;
}

TEST(KeilGlcd, RepeatedDrawingIsIdentical) {
    auto draw = [] {
        Screen screen;
        GLCD_SetFont(&GLCD_Font_16x24);
        GLCD_DrawString(8, 8, "MCB1700");
        GLCD_DrawBargraph(8, 200, 300, 20, 60);
        return screen.glcd().hash();
    };
    const auto first = draw();
    EXPECT_EQ(first, draw());
    Screen blank;
    EXPECT_NE(first, blank.glcd().hash());
}
