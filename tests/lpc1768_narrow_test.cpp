// Byte and halfword access to the GPIO registers (LPC17xx.h's FIOnXXXm / FIOnXXXL/H).
// A narrow store changes only its own lanes. The simulator could not confirm this
// with CPU-executed STRB/STRH (spikes/uvsim-script/e8-gpio-subword.ini); these tests
// pin down the lane-only behaviour the model implements.
#include "lpc17xx/lpc1768.hpp"

#include <gtest/gtest.h>

using latasim::lpc17xx::bit_band_alias;
using latasim::lpc17xx::BusFault;
using latasim::lpc17xx::Lpc1768;

namespace {

constexpr std::uint32_t FIO1DIR = 0x2009C020;
constexpr std::uint32_t FIO1MASK = 0x2009C030;
constexpr std::uint32_t FIO1PIN = 0x2009C034;
constexpr std::uint32_t FIO1SET = 0x2009C038;
constexpr std::uint32_t FIO1CLR = 0x2009C03C;
constexpr std::uint32_t FIO2SET = 0x2009C058;

// E8's setup: P1.0-P1.23 outputs, P1.24-P1.31 inputs (pulled high), latch 0x1, P1.0 masked.
Lpc1768 e8_setup() {
    Lpc1768 mcu;
    mcu.write32(FIO1DIR, 0x00FFFFFF);
    mcu.write32(FIO1PIN, 0x00000001);
    mcu.write32(FIO1MASK, 0x00000001);
    return mcu;
}

}  // namespace

TEST(Lpc1768Narrow, ByteStoreToFioPinChangesOnlyItsLaneAndKeepsMaskedBits) {
    Lpc1768 mcu = e8_setup();
    mcu.write8(FIO1PIN + 0, 0xFE);  // FIO1PIN0
    EXPECT_EQ(mcu.read32(FIO1SET), 0x000000FFu) << "bit 0 masked (kept 1), bits 1-7 written, byte 3 untouched";
    EXPECT_EQ(mcu.read32(FIO1PIN), 0xFF0000FEu) << "byte 3 still shows the inputs; masked bit 0 reads 0";
}

TEST(Lpc1768Narrow, HalfwordStoreToFioPinLowHalf) {
    Lpc1768 mcu;
    mcu.write32(FIO1DIR, 0x00FFFFFF);
    mcu.write16(FIO1PIN + 0, 0xABCD);  // FIO1PINL
    EXPECT_EQ(mcu.read32(FIO1SET), 0x0000ABCDu);
    EXPECT_EQ(mcu.read32(FIO1PIN), 0xFF00ABCDu);
}

TEST(Lpc1768Narrow, SetAndClrLanesActOnlyOnTheirBits) {
    Lpc1768 mcu;
    mcu.write32(FIO1DIR, 0xFFFFFFFF);
    mcu.write16(FIO1SET + 2, 0x1000);  // FIO1SETH: bit 28
    EXPECT_EQ(mcu.read32(FIO1SET), 1u << 28);
    mcu.write8(FIO1SET + 0, 0x01);     // FIO1SET0: bit 0
    EXPECT_EQ(mcu.read32(FIO1SET), (1u << 28) | 1u);
    mcu.write8(FIO1CLR + 3, 0x10);     // FIO1CLR3: bit 28
    EXPECT_EQ(mcu.read32(FIO1SET), 1u);
    EXPECT_TRUE(mcu.gpio().pin_level(1, 0));
    EXPECT_FALSE(mcu.gpio().pin_level(1, 28));
}

TEST(Lpc1768Narrow, DirAndMaskByteStoresReplaceOnlyTheirLane) {
    Lpc1768 mcu;
    mcu.write32(FIO1DIR, 0xFFFFFFFF);
    mcu.write8(FIO1DIR + 3, 0x00);  // FIO1DIR3
    EXPECT_EQ(mcu.read32(FIO1DIR), 0x00FFFFFFu);
    mcu.write32(FIO1MASK, 0xFFFFFFFF);
    mcu.write16(FIO1MASK + 0, 0x0000);  // FIO1MASKL
    EXPECT_EQ(mcu.read32(FIO1MASK), 0xFFFF0000u);
}

TEST(Lpc1768Narrow, NarrowReadsReturnTheWordsLanes) {
    Lpc1768 mcu = e8_setup();
    mcu.write32(FIO1SET, 0x12345678);
    for (const std::uint32_t reg : {FIO1DIR, FIO1MASK, FIO1PIN, FIO1SET}) {
        const std::uint32_t word = mcu.read32(reg);
        for (unsigned b = 0; b < 4; ++b)
            EXPECT_EQ(mcu.read8(reg + b), (word >> (8 * b)) & 0xFFu) << std::hex << reg << " byte " << b;
        EXPECT_EQ(mcu.read16(reg + 0), word & 0xFFFFu);
        EXPECT_EQ(mcu.read16(reg + 2), word >> 16);
    }
    EXPECT_EQ(mcu.read8(FIO1CLR + 3), 0u) << "FIOCLR is write-only at every width";
    EXPECT_EQ(mcu.read16(FIO1CLR), 0u);
}

TEST(Lpc1768Narrow, FourByteStoresEqualOneWordStore) {
    Lpc1768 bytes, word;
    bytes.write32(0x2009C040, 0xFFFFFFFF);  // FIO2DIR
    word.write32(0x2009C040, 0xFFFFFFFF);
    const std::uint8_t lanes[4] = {0xA5, 0x52, 0x29, 0x14};
    for (unsigned b = 0; b < 4; ++b) bytes.write8(FIO2SET + b, lanes[b]);
    word.write32(FIO2SET, 0x142952A5u);
    EXPECT_EQ(bytes.read32(0x2009C054), word.read32(0x2009C054));
}

TEST(Lpc1768Narrow, MisalignedOrReservedNarrowAccessesFault) {
    Lpc1768 mcu;
    EXPECT_THROW(mcu.read16(FIO1PIN + 1), BusFault) << "halfword not 2-aligned";
    EXPECT_THROW(mcu.write16(FIO1SET + 3, 1), BusFault);
    EXPECT_THROW(mcu.read32(FIO1PIN + 2), BusFault) << "word not 4-aligned";
    EXPECT_THROW(mcu.read8(0x2009C024), BusFault) << "reserved offset 0x04";
    EXPECT_THROW(mcu.write8(0x2009C0A0, 0), BusFault) << "past port 4";
    EXPECT_THROW(mcu.read8(0x40034000), BusFault) << "unmapped peripheral";
}

TEST(Lpc1768Narrow, BitBandAliasesAcceptWordAccessOnly) {
    Lpc1768 mcu;
    const std::uint32_t alias = bit_band_alias(FIO1PIN, 28);
    EXPECT_THROW(mcu.read8(alias), BusFault);
    EXPECT_THROW(mcu.write16(alias, 1), BusFault);
    EXPECT_NO_THROW(mcu.write32(alias, 1));
}
