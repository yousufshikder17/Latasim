// The LPC1768 ADC (lpc17xx/adc.hpp) through the MMIO path.
#include "boards/mcb1700/board.hpp"
#include "lpc17xx/lpc1768.hpp"
#include "lpc17xx/nvic.hpp"
#include "trace/trace.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>
#include <vector>

using latasim::lpc17xx::BusFault;
using latasim::lpc17xx::kAdcIrq;
using latasim::lpc17xx::Lpc1768;
using latasim::lpc17xx::NotModelled;
using latasim::mcb1700::Board;

namespace {

constexpr std::uint32_t ADCR = 0x40034000;
constexpr std::uint32_t ADGDR = 0x40034004;
constexpr std::uint32_t ADINTEN = 0x4003400C;
constexpr std::uint32_t ADDR2 = 0x40034018;
constexpr std::uint32_t ADSTAT = 0x40034030;
constexpr std::uint32_t PCLKSEL0 = 0x400FC1A8;
constexpr std::uint32_t ISER0 = 0xE000E100;

// Keil's ADC_Initialize and ADC_StartConversion values: AD0.2, CLKDIV 4, PDN.
constexpr std::uint32_t kConfig = (1u << 2) | (4u << 8) | (1u << 21);
constexpr std::uint32_t kStart = 1u << 24;
constexpr std::uint64_t kConversion = 65 * 5 * 4;  // 65 clocks of 25 MHz / 5: 1300 cycles

std::uint32_t result(std::uint32_t data) { return (data >> 4) & 0xFFFu; }

}  // namespace

TEST(Adc, ResetState) {
    Lpc1768 mcu;
    EXPECT_EQ(mcu.read32(ADCR), 0x01u);
    EXPECT_EQ(mcu.read32(ADINTEN), 0x100u) << "ADGINTEN: the global DONE interrupts";
    EXPECT_EQ(mcu.read32(ADSTAT), 0u);
    EXPECT_EQ(mcu.read32(ADGDR) >> 31, 0u);
    EXPECT_EQ(mcu.adc_conversion_cycles(), 65u * 1 * 4) << "CLKDIV 0, PCLK_ADC = CCLK/4";
}

TEST(Adc, ConversionTakesSixtyFiveAdcClocks) {
    Board board;
    auto& mcu = board.mcu();
    board.set_potentiometer(0x800);
    mcu.write32(ADCR, kConfig | kStart);
    EXPECT_TRUE(mcu.adc().busy());
    mcu.advance_cycles(kConversion - 1);
    EXPECT_EQ(mcu.peek32(ADGDR) >> 31, 0u) << "not a cycle early";
    mcu.advance_cycles(1);
    const std::uint32_t data = mcu.read32(ADGDR);
    EXPECT_EQ(data >> 31, 1u);
    EXPECT_EQ((data >> 24) & 7u, 2u) << "CHN";
    EXPECT_EQ(result(data), 0x800u);
}

TEST(Adc, PclkselAndClkdivSetTheConversionTime) {
    Lpc1768 mcu;
    mcu.write32(PCLKSEL0, 1u << 24);  // PCLK_ADC = CCLK
    mcu.write32(ADCR, (1u << 2) | (7u << 8) | (1u << 21) | kStart);  // CLKDIV 7: 12.5 MHz
    EXPECT_EQ(mcu.adc().cycles_to_done(), 65u * 8);
}

TEST(Adc, ResultsAtTheEndsAndMiddle) {
    for (std::uint32_t input : {0x000u, 0x7FFu, 0xFFFu}) {
        Board board;
        board.set_potentiometer(input);
        board.mcu().write32(ADCR, kConfig | kStart);
        board.mcu().advance_cycles(kConversion);
        EXPECT_EQ(result(board.mcu().read32(ADGDR)), input);
        EXPECT_EQ(result(board.mcu().read32(ADDR2)), input);
    }
    Board board;
    EXPECT_THROW(board.set_potentiometer(0x1000), std::out_of_range);
}

TEST(Adc, DoneFlagsClearAsTheManualSays) {
    Lpc1768 mcu;
    mcu.write32(ADCR, kConfig | kStart);
    mcu.advance_cycles(kConversion);
    EXPECT_EQ(mcu.read32(ADSTAT) & 0x4u, 0x4u) << "DONE2 mirrored";
    EXPECT_EQ(mcu.read32(ADGDR) >> 31, 1u);
    EXPECT_EQ(mcu.read32(ADGDR) >> 31, 0u) << "reading ADGDR cleared it";
    EXPECT_EQ(mcu.read32(ADDR2) >> 31, 1u) << "the channel's flag is separate";
    EXPECT_EQ(mcu.read32(ADDR2) >> 31, 0u);
    mcu.write32(ADCR, kConfig | kStart);
    mcu.advance_cycles(kConversion);
    mcu.write32(ADCR, kConfig);  // writing ADCR clears the global DONE
    EXPECT_EQ(mcu.read32(ADGDR) >> 31, 0u);
}

TEST(Adc, SampledWhenTheConversionStarts) {
    Board board;
    board.set_potentiometer(0x100);
    board.mcu().write32(ADCR, kConfig | kStart);
    board.set_potentiometer(0x200);  // during the conversion
    board.mcu().advance_cycles(kConversion);
    EXPECT_EQ(result(board.mcu().read32(ADGDR)), 0x100u);
    board.mcu().write32(ADCR, kConfig | kStart);  // the next one sees the new level
    board.mcu().advance_cycles(kConversion);
    EXPECT_EQ(result(board.mcu().read32(ADGDR)), 0x200u);
}

TEST(Adc, PoweredDownDoesNotConvert) {
    Lpc1768 mcu;
    mcu.write32(ADCR, (1u << 2) | kStart);  // PDN = 0
    EXPECT_FALSE(mcu.adc().busy());
}

TEST(Adc, GlobalInterruptThroughTheNvic) {
    Board board;
    auto& mcu = board.mcu();
    board.set_potentiometer(0xABC);
    std::vector<std::uint32_t> seen;
    mcu.bind_handler(kAdcIrq, [&] { seen.push_back(result(mcu.read32(ADGDR))); });  // the read clears DONE
    mcu.write32(ISER0, 1u << kAdcIrq);
    mcu.write32(ADCR, kConfig | kStart);
    mcu.advance_cycles(kConversion);
    EXPECT_EQ(seen, (std::vector<std::uint32_t>{0xABC}));
    EXPECT_FALSE(mcu.nvic().pending(kAdcIrq)) << "the line dropped when DONE was cleared";
    const auto& events = mcu.trace().events();
    EXPECT_EQ(to_string(events.at(2)).substr(6), "t=1300       adc     AD0.2     0xABC") << "after ISER0, ADCR";
}

TEST(Adc, InterruptOffWithAdinten0) {
    Lpc1768 mcu;
    mcu.write32(ADINTEN, 0);
    mcu.write32(ADCR, kConfig | kStart);
    mcu.advance_cycles(kConversion);
    EXPECT_FALSE(mcu.nvic().pending(kAdcIrq));
    EXPECT_EQ(mcu.read32(ADSTAT), 0x4u) << "DONE2 set, ADINT not";
}

TEST(Adc, ChannelInterruptModeClearsOnTheChannelRegister) {
    Lpc1768 mcu;
    mcu.write32(ADINTEN, 1u << 2);  // ADGINTEN off, channel 2 on
    mcu.write32(ADCR, kConfig | kStart);
    mcu.advance_cycles(kConversion);
    EXPECT_TRUE(mcu.nvic().pending(kAdcIrq));
    EXPECT_EQ(mcu.read32(ADSTAT) >> 16, 1u) << "ADINT";
    mcu.read32(ADGDR);
    mcu.write32(0xE000E280, 1u << kAdcIrq);  // ICPR: re-pends while the line is up
    EXPECT_TRUE(mcu.nvic().pending(kAdcIrq)) << "ADGDR does not clear DONE2";
    mcu.read32(ADDR2);
    mcu.write32(0xE000E280, 1u << kAdcIrq);
    EXPECT_FALSE(mcu.nvic().pending(kAdcIrq));
}

TEST(Adc, RestartingDuringAConversionStartsOver) {
    Lpc1768 mcu;
    mcu.write32(ADCR, kConfig | kStart);
    mcu.advance_cycles(1000);
    mcu.write32(ADCR, kConfig | kStart);
    mcu.advance_cycles(1000);
    EXPECT_TRUE(mcu.adc().busy());
    mcu.advance_cycles(300);
    EXPECT_FALSE(mcu.adc().busy());
}

TEST(Adc, UnsupportedAccessesAreExplicit) {
    Lpc1768 mcu;
    EXPECT_THROW(mcu.write32(ADCR, kConfig | (1u << 16)), NotModelled) << "burst mode";
    EXPECT_THROW(mcu.write32(ADCR, kConfig | (2u << 24)), NotModelled) << "START on EINT0 edge";
    EXPECT_THROW(mcu.write32(ADDR2, 0), BusFault) << "read-only";
    EXPECT_THROW(mcu.write32(ADSTAT, 0), BusFault) << "read-only";
    EXPECT_THROW(mcu.read32(0x40034034), BusFault) << "ADTRM not modelled";
    EXPECT_THROW(mcu.read16(ADCR), BusFault);
    EXPECT_EQ(mcu.read32(ADCR), 0x01u) << "failed writes change nothing";
}

TEST(Adc, RepeatedRunsAreIdentical) {
    auto run = [] {
        Board board;
        auto& mcu = board.mcu();
        mcu.bind_handler(kAdcIrq, [&] { mcu.read32(ADGDR); });
        mcu.write32(ISER0, 1u << kAdcIrq);
        for (std::uint32_t v = 0; v < 8; ++v) {
            board.set_potentiometer(v * 500);
            mcu.write32(ADCR, kConfig | kStart);
            mcu.advance_cycles(2000);
        }
        return mcu.trace().events();
    };
    EXPECT_EQ(run(), run());
}
