#include "lpc17xx/gpio.hpp"

#include <gtest/gtest.h>

#include <stdexcept>

using vwb::lpc17xx::Gpio;
using vwb::lpc17xx::GpioReg;
using vwb::lpc17xx::kBondedPins;

namespace {

constexpr std::uint32_t kP1_28 = 1u << 28;

}  // namespace

TEST(Gpio, ResetStateMatchesSimulator) {
    const Gpio gpio;
    for (unsigned port = 0; port < Gpio::kPortCount; ++port) {
        EXPECT_EQ(gpio.read(port, GpioReg::Dir), 0u);
        EXPECT_EQ(gpio.read(port, GpioReg::Mask), 0u);
        EXPECT_EQ(gpio.read(port, GpioReg::Set), 0u);
        EXPECT_EQ(gpio.read(port, GpioReg::Clr), 0u);
        // E1: every bonded-out pin is an input pulled high.
        EXPECT_EQ(gpio.read(port, GpioReg::Pin), kBondedPins[port]) << "port " << port;
    }
}

TEST(Gpio, DirSelectsWhetherThePinShowsTheLatch) {
    Gpio gpio;
    gpio.write(1, GpioReg::Clr, kP1_28);
    EXPECT_TRUE(gpio.pin_level(1, 28)) << "input: pulled high, latch ignored";
    gpio.write(1, GpioReg::Dir, kP1_28);
    EXPECT_TRUE(gpio.is_output(1, 28));
    EXPECT_FALSE(gpio.pin_level(1, 28)) << "output: shows latch (0)";
}

TEST(Gpio, SetDrivesAnOutputHighAndClrDrivesItLow) {
    Gpio gpio;
    gpio.write(1, GpioReg::Dir, kP1_28);
    gpio.write(1, GpioReg::Set, kP1_28);
    EXPECT_TRUE(gpio.pin_level(1, 28));
    gpio.write(1, GpioReg::Clr, kP1_28);
    EXPECT_FALSE(gpio.pin_level(1, 28));
}

TEST(Gpio, SetAndClrLeaveZeroBitsUnchanged) {
    Gpio gpio;
    gpio.write(1, GpioReg::Dir, 0xFFFFFFFF);
    gpio.write(1, GpioReg::Set, 0x0000FFFF);
    gpio.write(1, GpioReg::Set, 0x00000000);
    gpio.write(1, GpioReg::Clr, 0x00000000);
    EXPECT_EQ(gpio.read(1, GpioReg::Set), 0x0000FFFFu);
    gpio.write(1, GpioReg::Clr, 0x000000F0);
    EXPECT_EQ(gpio.read(1, GpioReg::Set), 0x0000FF0Fu);
}

TEST(Gpio, SetOnAnInputUpdatesTheLatchButNotThePin) {  // E7 R1, R2
    Gpio gpio;
    gpio.write(1, GpioReg::Clr, kP1_28);
    gpio.write(1, GpioReg::Set, kP1_28);
    EXPECT_EQ(gpio.read(1, GpioReg::Set) & kP1_28, kP1_28);
    gpio.set_external_level(1, 28, false);
    EXPECT_FALSE(gpio.pin_level(1, 28)) << "input still shows the external level";
    gpio.write(1, GpioReg::Dir, kP1_28);
    EXPECT_TRUE(gpio.pin_level(1, 28)) << "the latched value appears once it is an output";
}

TEST(Gpio, SetReadsTheLatchNotThePinLevel) {
    Gpio gpio;  // all inputs pulled high, latch 0
    EXPECT_EQ(gpio.read(1, GpioReg::Set), 0u);
    EXPECT_EQ(gpio.read(1, GpioReg::Pin), kBondedPins[1]);
}

TEST(Gpio, ClrIsWriteOnlyAndReadsZero) {  // E7 R3
    Gpio gpio;
    gpio.write(1, GpioReg::Set, 0xFFFFFFFF);
    gpio.write(1, GpioReg::Clr, kP1_28);
    EXPECT_EQ(gpio.read(1, GpioReg::Clr), 0u);
}

TEST(Gpio, PinWriteSetsTheLatchOfEveryUnmaskedBitIncludingInputs) {  // E7 R6, R8
    Gpio gpio;
    gpio.write(1, GpioReg::Pin, 0x12345678);
    EXPECT_EQ(gpio.read(1, GpioReg::Set), 0x12345678u);
    EXPECT_EQ(gpio.read(1, GpioReg::Pin), kBondedPins[1]) << "all inputs: pins unchanged";
}

TEST(Gpio, InputPinReadsTheExternalLevel) {  // E7 R10
    Gpio gpio;
    gpio.set_external_level(1, 20, false);
    EXPECT_EQ(gpio.read(1, GpioReg::Pin), kBondedPins[1] & ~(1u << 20));
    gpio.set_external_level(1, 20, true);
    EXPECT_EQ(gpio.read(1, GpioReg::Pin), kBondedPins[1]);
}

TEST(Gpio, MaskBlocksSetClrAndPinWrites) {  // E7 R5, R6
    Gpio gpio;
    gpio.write(1, GpioReg::Dir, kP1_28);
    gpio.write(1, GpioReg::Mask, kP1_28);
    gpio.write(1, GpioReg::Set, kP1_28);
    EXPECT_FALSE(gpio.pin_level(1, 28));
    gpio.write(1, GpioReg::Pin, 0xFFFFFFFF);
    EXPECT_EQ(gpio.read(1, GpioReg::Set), ~kP1_28);
    gpio.write(1, GpioReg::Mask, 0);
    gpio.write(1, GpioReg::Set, kP1_28);
    gpio.write(1, GpioReg::Mask, kP1_28);
    gpio.write(1, GpioReg::Clr, kP1_28);
    EXPECT_TRUE(gpio.pin_level(1, 28)) << "masked CLR has no effect";
}

TEST(Gpio, MaskedBitsReadZeroInFioPinEvenWhenThePinIsHigh) {  // E7 R13, R14
    Gpio gpio;
    gpio.write(1, GpioReg::Dir, kP1_28);
    gpio.write(1, GpioReg::Set, kP1_28);
    gpio.write(1, GpioReg::Mask, kP1_28 | 1u);  // an output driven high and an input pulled high
    EXPECT_TRUE(gpio.pin_level(1, 28));
    EXPECT_TRUE(gpio.pin_level(1, 0));
    EXPECT_EQ(gpio.read(1, GpioReg::Pin), kBondedPins[1] & ~(kP1_28 | 1u));
    EXPECT_EQ(gpio.read(1, GpioReg::Set), kP1_28) << "FIOSET reads the latch; the mask does not apply";
}

TEST(Gpio, UnbondedPinsReadLowAsInputsButFollowTheLatchAsOutputs) {  // E7 R0, R15
    Gpio gpio;
    EXPECT_FALSE(gpio.pin_level(1, 2)) << "P1.2 is not bonded out";
    gpio.write(1, GpioReg::Dir, 0xFFFFFFFF);
    gpio.write(1, GpioReg::Set, 0xFFFFFFFF);
    EXPECT_EQ(gpio.read(1, GpioReg::Pin), 0xFFFFFFFFu);
}

TEST(Gpio, PortsAreIndependent) {
    Gpio gpio;
    gpio.write(2, GpioReg::Dir, 1u << 2);
    gpio.write(2, GpioReg::Clr, 1u << 2);
    EXPECT_FALSE(gpio.pin_level(2, 2));
    EXPECT_EQ(gpio.read(1, GpioReg::Pin), kBondedPins[1]);
    EXPECT_EQ(gpio.read(1, GpioReg::Dir), 0u);
}

TEST(Gpio, OutOfRangePortOrPinThrows) {
    Gpio gpio;
    EXPECT_THROW(gpio.read(5, GpioReg::Pin), std::out_of_range);
    EXPECT_THROW(gpio.write(5, GpioReg::Set, 1), std::out_of_range);
    EXPECT_THROW(gpio.pin_level(1, 32), std::out_of_range);
    EXPECT_THROW(gpio.set_external_level(0, 32, true), std::out_of_range);
}

// Replays experiment E7 (spikes/uvsim-script/e7-gpio-semantics.ini) register by
// register and checks every value µVision's LPC1768 simulator reported
// (spikes/uvsim-script/e7-run1.out).
TEST(Gpio, ReplayOfSimulatorExperimentE7MatchesEveryReading) {
    Gpio g;
    auto expect = [&](std::uint32_t pin, std::uint32_t set) {
        EXPECT_EQ(g.read(1, GpioReg::Pin), pin);
        EXPECT_EQ(g.read(1, GpioReg::Set), set);
    };
    expect(0xFFFFC713, 0x00000000);                                    // R0
    g.write(1, GpioReg::Set, kP1_28);  expect(0xFFFFC713, 0x10000000);  // R1
    g.write(1, GpioReg::Dir, kP1_28);  expect(0xFFFFC713, 0x10000000);  // R2
    g.write(1, GpioReg::Clr, kP1_28);  expect(0xEFFFC713, 0x00000000);  // R3
    EXPECT_EQ(g.read(1, GpioReg::Clr), 0u);
    g.write(1, GpioReg::Mask, kP1_28); expect(0xEFFFC713, 0x00000000);  // R4
    g.write(1, GpioReg::Set, kP1_28);  expect(0xEFFFC713, 0x00000000);  // R5
    g.write(1, GpioReg::Pin, 0xFFFFFFFF); expect(0xEFFFC713, 0xEFFFFFFF);  // R6
    g.write(1, GpioReg::Mask, 0);      expect(0xEFFFC713, 0xEFFFFFFF);  // R7
    g.write(1, GpioReg::Pin, 0);       expect(0xEFFFC713, 0x00000000);  // R8
    g.write(1, GpioReg::Dir, 0);       expect(0xFFFFC713, 0x00000000);  // R9
    g.set_external_level(1, 20, false);
    EXPECT_EQ(g.read(1, GpioReg::Pin), 0xFFEFC713u);                    // R10
    g.write(1, GpioReg::Dir, 0xFFFFFFFF);
    EXPECT_EQ(g.read(1, GpioReg::Dir), 0xFFFFFFFFu);
    EXPECT_EQ(g.read(1, GpioReg::Pin), 0x00000000u);                    // R11
    // µVision's PORT1 write in R10 is not a lasting external driver: after the pin
    // was an output (R11) it read as pulled high again (R12). Release it the same way.
    g.set_external_level(1, 20, true);
    g.write(1, GpioReg::Dir, kP1_28);
    g.write(1, GpioReg::Set, kP1_28);  expect(0xFFFFC713, 0x10000000);  // R12
    g.write(1, GpioReg::Mask, kP1_28); expect(0xEFFFC713, 0x10000000);  // R13
    g.write(1, GpioReg::Mask, 1);
    EXPECT_EQ(g.read(1, GpioReg::Pin), 0xFFFFC712u);                    // R14
    g.write(1, GpioReg::Mask, 0);
    g.write(1, GpioReg::Dir, 0xFFFFFFFF);
    g.write(1, GpioReg::Set, 0xFFFFFFFF); expect(0xFFFFFFFF, 0xFFFFFFFF);  // R15
}
