// PCONP: stored, never gating (docs/phase2/open-questions.md, question 3).
#include "gpio_snapshot.hpp"
#include "lpc17xx/keil_gpio_driver.hpp"
#include "lpc17xx/lpc1768.hpp"

#include <gtest/gtest.h>

using latasim::lpc17xx::BusFault;
using latasim::lpc17xx::KeilGpioDriver;
using latasim::lpc17xx::kPconpAddress;
using latasim::lpc17xx::kPconpGpio;
using latasim::lpc17xx::kPconpReset;
using latasim::lpc17xx::Lpc1768;

TEST(Pconp, ResetValueIsTable46sAndTheSimulators) {
    Lpc1768 mcu;
    EXPECT_EQ(mcu.read32(kPconpAddress), 0x042887DEu) << "E9 RESET line";
    EXPECT_EQ(mcu.read32(kPconpAddress) & kPconpGpio, kPconpGpio) << "PCGPIO set at reset";
}

TEST(Pconp, ReadsBackWhatWasWritten) {
    Lpc1768 mcu;
    mcu.write32(kPconpAddress, 0);
    EXPECT_EQ(mcu.read32(kPconpAddress), 0u);
    mcu.write32(kPconpAddress, kPconpReset);  // what Keil's SystemInit writes
    EXPECT_EQ(mcu.pconp(), kPconpReset);
}

TEST(Pconp, OnlyWordAccessToPconpItselfIsMapped) {
    Lpc1768 mcu;
    EXPECT_THROW(mcu.read8(kPconpAddress), BusFault);
    EXPECT_THROW(mcu.write16(kPconpAddress, 0), BusFault);
    EXPECT_THROW(mcu.read32(kPconpAddress - 4), BusFault) << "PCON is not modeled";
    EXPECT_THROW(mcu.write32(kPconpAddress + 4, 0), BusFault);
    EXPECT_EQ(mcu.pconp(), kPconpReset) << "failed accesses change nothing";
}

// E9 CP9: with PCGPIO clear, a FIOSET store still reaches the latch.
TEST(Pconp, ClearingPcgpioDoesNotGateGpio) {
    Lpc1768 mcu;
    mcu.write32(kPconpAddress, kPconpReset & ~kPconpGpio);
    mcu.write32(0x2009C020, 0xFFFFFFFF);  // FIO1DIR
    mcu.write32(0x2009C038, 0x00000100);  // FIO1SET
    EXPECT_EQ(mcu.read32(0x2009C034), 0x00000100u);
}

TEST(KeilGpioDriver, PortClockSetsAndClearsOnlyPcgpio) {
    Lpc1768 mcu;
    KeilGpioDriver gpio(mcu);
    gpio.port_clock(false);
    EXPECT_EQ(mcu.pconp(), kPconpReset & ~kPconpGpio);
    gpio.port_clock(true);
    EXPECT_EQ(mcu.pconp(), kPconpReset);
}
