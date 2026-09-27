#pragma once
// LPC17xx fast GPIO (FIO) register model.
//
// Semantics come from NXP's register description (LPC176x5x.svd in LPC1700_DFP)
// and were checked against µVision's LPC1768 simulator in experiment E7
// (spikes/uvsim-script/e7-gpio-semantics.ini / e7-run1.out):
//
//   FIODIR   read/write storage. 1 = output.
//   FIOMASK  read/write storage. 1 = pin ignores FIOSET/FIOCLR/FIOPIN writes
//            and reads as 0 in FIOPIN.
//   FIOSET   write: latch |= value & ~mask.  read: the output latch (mask ignored).
//   FIOCLR   write: latch &= ~(value & ~mask). read: 0 (write-only register).
//   FIOPIN   write: latch = (latch & mask) | (value & ~mask), for inputs too.
//            read:  pin level & ~mask.
//   level    output pin: its latch bit. input pin: the external level.
//
// All five ports keep 32 bits of state. Pins the package doesn't bond out read
// low as inputs but follow the latch as outputs, as in the simulator (E7 R0/R15).
#include <array>
#include <cstdint>

namespace latasim::lpc17xx {

// Register offsets within one port, as in LPC17xx.h (LPC_GPIO_TypeDef).
enum class GpioReg : std::uint32_t {
    Dir = 0x00,
    Mask = 0x10,
    Pin = 0x14,
    Set = 0x18,
    Clr = 0x1C,
};

class Gpio {
public:
    static constexpr unsigned kPortCount = 5;

    Gpio();

    // What a firmware load or store of LPC_GPIOn->FIOxxx does. `lanes` restricts a
    // store to some byte lanes: the byte and halfword views in LPC17xx.h
    // (FIO1PIN0, FIO1SETH, ...) are separate registers, so a narrow store changes only
    // its own lanes, with the usual rules inside them. 0xFFFFFFFF is a full word.
    std::uint32_t read(unsigned port, GpioReg reg) const;
    void write(unsigned port, GpioReg reg, std::uint32_t value, std::uint32_t lanes = 0xFFFFFFFF);

    // Board side: the level driven onto a pin from outside the MCU. It is only
    // visible while the pin is an input. Default: high (PINMODE resets to
    // pull-up) for bonded-out pins, low for the rest.
    void set_external_level(unsigned port, unsigned pin, bool high);

    // Physical pin level, regardless of FIOMASK.
    bool pin_level(unsigned port, unsigned pin) const;
    bool is_output(unsigned port, unsigned pin) const;

private:
    struct Port {
        std::uint32_t dir = 0;
        std::uint32_t mask = 0;
        std::uint32_t latch = 0;
        std::uint32_t external = 0;

        std::uint32_t level() const { return (latch & dir) | (external & ~dir); }
    };

    Port& port_at(unsigned port);
    const Port& port_at(unsigned port) const;

    std::array<Port, kPortCount> ports_;
};

// Pins bonded out on the LPC1768 (LQFP100): FIOnPIN at reset in the simulator
// (E1, spikes/uvsim-script/e1-run1.out), all inputs pulled high.
inline constexpr std::array<std::uint32_t, Gpio::kPortCount> kBondedPins{
    0x7FFF8FFF, 0xFFFFC713, 0x00003FFF, 0x06000000, 0x30000000};

}  // namespace latasim::lpc17xx
