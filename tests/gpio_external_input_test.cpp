// External input semantics of the GPIO model. Four separate things per pin:
//   latch     (the output register: FIOSET/FIOCLR/FIOPIN writes, FIOSET reads)
//   direction (FIODIR)
//   external  (what the board drives onto the pin)
//   level     (what the pin is: latch if output, external if input; FIOPIN reads it)
// An external input never writes the latch, and the latch never shows on an input.
#include "lpc17xx/gpio.hpp"

#include <gtest/gtest.h>

using latasim::lpc17xx::Gpio;
using latasim::lpc17xx::GpioReg;

namespace {

constexpr unsigned kPort = 1;
constexpr unsigned kPin = 23;
constexpr std::uint32_t kBit = 1u << kPin;

bool fiopin_bit(const Gpio& g) { return (g.read(kPort, GpioReg::Pin) & kBit) != 0; }

}  // namespace

TEST(GpioExternalInput, ExternallyDrivenHighAndLowAreRead) {
    Gpio g;
    g.set_external_level(kPort, kPin, false);
    EXPECT_FALSE(g.pin_level(kPort, kPin));
    EXPECT_FALSE(fiopin_bit(g));
    g.set_external_level(kPort, kPin, true);
    EXPECT_TRUE(g.pin_level(kPort, kPin));
    EXPECT_TRUE(fiopin_bit(g));
}

TEST(GpioExternalInput, ChangesAreVisibleOnTheNextRead) {
    Gpio g;
    for (const bool high : {false, true, false, false, true}) {
        g.set_external_level(kPort, kPin, high);
        EXPECT_EQ(fiopin_bit(g), high);
    }
}

TEST(GpioExternalInput, AnExternalLevelNeverWritesTheLatch) {
    Gpio g;
    g.write(kPort, GpioReg::Set, kBit);
    g.set_external_level(kPort, kPin, false);
    EXPECT_EQ(g.read(kPort, GpioReg::Set), kBit);
    g.write(kPort, GpioReg::Clr, kBit);
    g.set_external_level(kPort, kPin, true);
    EXPECT_EQ(g.read(kPort, GpioReg::Set), 0u);
}

TEST(GpioExternalInput, AnOutputShowsItsLatchWhateverIsDrivenExternally) {
    Gpio g;
    g.write(kPort, GpioReg::Dir, kBit);
    g.write(kPort, GpioReg::Set, kBit);
    g.set_external_level(kPort, kPin, false);
    EXPECT_TRUE(fiopin_bit(g));
    g.write(kPort, GpioReg::Clr, kBit);
    g.set_external_level(kPort, kPin, true);
    EXPECT_FALSE(fiopin_bit(g));
}

TEST(GpioExternalInput, SwitchingInputToOutputShowsTheLatch) {
    Gpio g;
    g.set_external_level(kPort, kPin, false);
    g.write(kPort, GpioReg::Set, kBit);  // latched while still an input
    EXPECT_FALSE(fiopin_bit(g));
    g.write(kPort, GpioReg::Dir, kBit);
    EXPECT_TRUE(fiopin_bit(g));
}

TEST(GpioExternalInput, SwitchingOutputToInputShowsTheExternalLevelAgain) {
    Gpio g;
    g.set_external_level(kPort, kPin, false);
    g.write(kPort, GpioReg::Dir, kBit);
    g.write(kPort, GpioReg::Set, kBit);
    EXPECT_TRUE(fiopin_bit(g));
    g.write(kPort, GpioReg::Dir, 0);
    EXPECT_FALSE(fiopin_bit(g)) << "the external level was kept while the pin was an output";
    EXPECT_EQ(g.read(kPort, GpioReg::Set), kBit) << "and so was the latch";
}

TEST(GpioExternalInput, FioPinWriteOnAnInputLatchesButDoesNotDriveThePin) {
    Gpio g;
    g.set_external_level(kPort, kPin, false);
    g.write(kPort, GpioReg::Pin, 0xFFFFFFFF);
    EXPECT_FALSE(fiopin_bit(g));
    EXPECT_EQ(g.read(kPort, GpioReg::Set) & kBit, kBit);
}

TEST(GpioExternalInput, MaskedInputReadsZeroButKeepsTrackingTheExternalLevel) {
    Gpio g;
    g.write(kPort, GpioReg::Mask, kBit);
    g.set_external_level(kPort, kPin, true);
    EXPECT_FALSE(fiopin_bit(g)) << "masked bits read 0";
    EXPECT_TRUE(g.pin_level(kPort, kPin)) << "the pin itself is high";
    g.set_external_level(kPort, kPin, false);
    g.write(kPort, GpioReg::Mask, 0);
    EXPECT_FALSE(fiopin_bit(g)) << "unmasked, the latest external level shows";
}

TEST(GpioExternalInput, OtherPinsOfThePortAreUnaffected) {
    Gpio g;
    const std::uint32_t before = g.read(kPort, GpioReg::Pin);
    g.set_external_level(kPort, kPin, false);
    EXPECT_EQ(g.read(kPort, GpioReg::Pin), before & ~kBit);
    EXPECT_EQ(g.read(2, GpioReg::Pin), latasim::lpc17xx::kBondedPins[2]);
}
