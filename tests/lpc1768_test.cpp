#include "lpc17xx/lpc1768.hpp"

#include <gtest/gtest.h>

using latasim::lpc17xx::bit_band_alias;
using latasim::lpc17xx::BusFault;
using latasim::lpc17xx::gpio_register_address;
using latasim::lpc17xx::GpioReg;
using latasim::lpc17xx::kBondedPins;
using latasim::lpc17xx::Lpc1768;

namespace {

// Register addresses as firmware spells them (LPC17xx.h: LPC_GPIOn->FIOxxx).
constexpr std::uint32_t FIO1DIR = 0x2009C020;
constexpr std::uint32_t FIO1MASK = 0x2009C030;
constexpr std::uint32_t FIO1PIN = 0x2009C034;
constexpr std::uint32_t FIO1SET = 0x2009C038;
constexpr std::uint32_t FIO1CLR = 0x2009C03C;
constexpr std::uint32_t FIO2DIR = 0x2009C040;
constexpr std::uint32_t FIO2PIN = 0x2009C054;
constexpr std::uint32_t FIO2SET = 0x2009C058;
constexpr std::uint32_t FIO2CLR = 0x2009C05C;

void set_bits(Lpc1768& mcu, std::uint32_t address, std::uint32_t bits) {  // firmware `reg |= bits`
    mcu.write32(address, mcu.read32(address) | bits);
}

}  // namespace

TEST(Lpc1768, GpioRegisterAddressesMatchLpc17xxH) {
    EXPECT_EQ(gpio_register_address(1, GpioReg::Dir), FIO1DIR);
    EXPECT_EQ(gpio_register_address(1, GpioReg::Mask), FIO1MASK);
    EXPECT_EQ(gpio_register_address(1, GpioReg::Pin), FIO1PIN);
    EXPECT_EQ(gpio_register_address(1, GpioReg::Set), FIO1SET);
    EXPECT_EQ(gpio_register_address(1, GpioReg::Clr), FIO1CLR);
    EXPECT_EQ(gpio_register_address(2, GpioReg::Pin), FIO2PIN);
    EXPECT_EQ(gpio_register_address(0, GpioReg::Dir), 0x2009C000u);
    EXPECT_EQ(gpio_register_address(4, GpioReg::Clr), 0x2009C09Cu);
}

TEST(Lpc1768, StoresAndLoadsReachTheGpioModel) {
    Lpc1768 mcu;
    mcu.write32(FIO1DIR, 1u << 28);
    mcu.write32(FIO1SET, 1u << 28);
    EXPECT_TRUE(mcu.gpio().pin_level(1, 28));
    EXPECT_EQ(mcu.read32(FIO1DIR), 1u << 28);
    mcu.write32(FIO1CLR, 1u << 28);
    EXPECT_FALSE(mcu.gpio().pin_level(1, 28));
    EXPECT_EQ(mcu.read32(FIO1PIN), kBondedPins[1] & ~(1u << 28));
}

TEST(Lpc1768, UnmappedReservedAndUnalignedAccessesFault) {
    Lpc1768 mcu;
    EXPECT_THROW(mcu.read32(0x2009C024), BusFault) << "reserved offset 0x04 in port 1";
    EXPECT_THROW(mcu.write32(0x2009C0A0, 0), BusFault) << "past port 4";
    EXPECT_THROW(mcu.read32(0x2009C035), BusFault) << "unaligned";
    EXPECT_THROW(mcu.read32(0x40034000), BusFault) << "ADC is not modeled";
    try {
        mcu.write32(0x40000000, 1);
        FAIL();
    } catch (const BusFault& fault) {
        EXPECT_EQ(fault.address(), 0x40000000u);
    }
}

TEST(Lpc1768, BitBandAliasFormulaMatchesFirmwareAddresses) {
    EXPECT_EQ(bit_band_alias(FIO1PIN, 28), 0x233806F0u);  // P1.28
    EXPECT_EQ(bit_band_alias(FIO1PIN, 27), 0x233806ECu);  // P1.27
    EXPECT_EQ(bit_band_alias(FIO2PIN, 2), 0x23380A88u);   // P2.2
    EXPECT_EQ(bit_band_alias(FIO1PIN, 0), 0x23380680u);
}

TEST(Lpc1768, BitBandReadReturnsOneBit) {
    Lpc1768 mcu;
    EXPECT_EQ(mcu.read32(bit_band_alias(FIO1PIN, 28)), 1u) << "input pulled high";
    mcu.gpio().set_external_level(1, 28, false);
    EXPECT_EQ(mcu.read32(bit_band_alias(FIO1PIN, 28)), 0u);
}

TEST(Lpc1768, BitBandWriteToFioPinChangesOnlyThatOutput) {
    Lpc1768 mcu;
    mcu.write32(FIO1DIR, (1u << 28) | (1u << 29));
    mcu.write32(FIO1SET, (1u << 28) | (1u << 29));
    mcu.write32(bit_band_alias(FIO1PIN, 28), 0);
    EXPECT_FALSE(mcu.gpio().pin_level(1, 28));
    EXPECT_TRUE(mcu.gpio().pin_level(1, 29));
    mcu.write32(bit_band_alias(FIO1PIN, 28), 1);
    EXPECT_TRUE(mcu.gpio().pin_level(1, 28));
}

TEST(Lpc1768, BitBandWritesToSetAndClrActLikeStoresOfThatBit) {
    Lpc1768 mcu;
    mcu.write32(FIO1DIR, 0xFFFFFFFF);
    mcu.write32(bit_band_alias(FIO1SET, 28), 1);
    EXPECT_EQ(mcu.read32(FIO1SET), 1u << 28);
    mcu.write32(bit_band_alias(FIO1SET, 28), 0);
    EXPECT_EQ(mcu.read32(FIO1SET), 1u << 28) << "0 to a SET bit has no effect";
    mcu.write32(bit_band_alias(FIO1CLR, 28), 1);
    EXPECT_EQ(mcu.read32(FIO1SET), 0u);
}

// The bus turns a bit-band store into read FIOPIN / modify / write FIOPIN, so the
// input pins' levels land in their latch bits. Invisible until they become outputs.
TEST(Lpc1768, BitBandFioPinWriteCopiesInputLevelsIntoTheLatch) {
    Lpc1768 mcu;
    mcu.write32(FIO1DIR, 1u << 28);
    mcu.write32(bit_band_alias(FIO1PIN, 28), 1);
    EXPECT_EQ(mcu.read32(FIO1SET), kBondedPins[1]) << "P1.27 etc. read high, so latched high";
    mcu.write32(FIO1DIR, 1u << 27);
    EXPECT_TRUE(mcu.gpio().pin_level(1, 27));
}

TEST(Lpc1768, BitBandAliasOutsideGpioFaultsWithTheAliasAddress) {
    Lpc1768 mcu;
    const std::uint32_t sram_alias = bit_band_alias(0x20000000, 0);
    try {
        mcu.write32(sram_alias, 1);
        FAIL();
    } catch (const BusFault& fault) {
        EXPECT_EQ(fault.address(), sram_alias);
    }
    EXPECT_THROW(mcu.read32(0x233806F1), BusFault) << "unaligned alias";
}

// Regression for the Phase 0 finding: a direct alias meant for P1.28 used P1.27's
// address. The P1.28 output must not change; P1.27 is an input and stays high.
TEST(Lpc1768, WrongPinAliasLeavesTheIntendedPinUnchanged) {
    Lpc1768 mcu;
    mcu.write32(FIO1DIR, 1u << 28);
    mcu.write32(FIO1SET, 1u << 28);
    mcu.write32(0x233806EC, 0);  // meant for P1.28, actually P1.27
    EXPECT_TRUE(mcu.gpio().pin_level(1, 28));
    EXPECT_TRUE(mcu.gpio().pin_level(1, 27));
    mcu.write32(0x233806F0, 0);  // the correct alias
    EXPECT_FALSE(mcu.gpio().pin_level(1, 28));
}

// Replays the bit-band test firmware's GPIO accesses, in order, and checks the
// FIO1PIN/FIO2PIN words the LPC1768 simulator reported after each step in E5
// (spikes/uvsim-script/e5-run1.out). Its ADC accesses are left out: not modeled.
TEST(Lpc1768, ReplayOfBitBandTestFirmwareMatchesSimulatorE5) {
    Lpc1768 mcu;
    auto expect = [&](std::uint32_t fio1pin, std::uint32_t fio2pin, const char* step) {
        EXPECT_EQ(mcu.read32(FIO1PIN), fio1pin) << step;
        EXPECT_EQ(mcu.read32(FIO2PIN), fio2pin) << step;
    };
    expect(0xFFFFC713, 0x00003FFF, "reset");
    set_bits(mcu, FIO1DIR, 1u << 28);  // LED setup
    set_bits(mcu, FIO2DIR, 1u << 2);
    mcu.write32(FIO1SET, 1u << 28);
    mcu.write32(FIO2SET, 1u << 2);

    mcu.write32(FIO1CLR, 1u << 28);  // masking "on"
    mcu.write32(FIO2CLR, 1u << 2);
    expect(0xEFFFC713, 0x00003FFB, "masking on");
    mcu.write32(FIO1SET, 1u << 28);  // masking "off"
    mcu.write32(FIO2SET, 1u << 2);
    expect(0xFFFFC713, 0x00003FFF, "masking off");

    mcu.write32(bit_band_alias(FIO1PIN, 28), 0);  // computed bit-band "on"
    mcu.write32(bit_band_alias(FIO2PIN, 2), 0);
    expect(0xEFFFC713, 0x00003FFB, "computed bit-band on");
    mcu.write32(bit_band_alias(FIO1PIN, 28), 1);  // computed bit-band "off"
    mcu.write32(bit_band_alias(FIO2PIN, 2), 1);
    expect(0xFFFFC713, 0x00003FFF, "computed bit-band off");

    mcu.write32(0x233806EC, 0);  // direct alias "on" (the wrong-pin alias)
    mcu.write32(0x23380A88, 0);
    expect(0xFFFFC713, 0x00003FFB, "direct alias on: P1.28 unchanged");
    mcu.write32(0x233806EC, 1);  // direct alias "off"
    mcu.write32(0x23380A88, 1);
    expect(0xFFFFC713, 0x00003FFF, "direct alias off");
}
