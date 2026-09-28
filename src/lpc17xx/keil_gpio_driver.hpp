#pragma once
// Host implementation of Keil's LPC17xx GPIO driver (RTE_Driver/GPIO_LPC17xx.c,
// LPC1700_DFP 2.6.0). Each call makes the same register accesses the real
// driver compiles to, through Lpc1768::read32/write32, so the board API path
// and direct register access share one model.
//
// Not modeled: GPIO_PortWrite/GPIO_PortRead (unused so far).
#include <cstdint>

#include "lpc17xx/lpc1768.hpp"

namespace latasim::lpc17xx {

class KeilGpioDriver {
public:
    explicit KeilGpioDriver(Lpc1768& mcu) : mcu_(mcu) {}

    // GPIO_PortClock: PCONP |= PCGPIO (on) or PCONP &= ~PCGPIO (off).
    void port_clock(bool on);
    // GPIO_SetDir: FIODIR |= bit (output) or FIODIR &= ~bit (input).
    void set_dir(unsigned port, unsigned pin, bool output);
    // GPIO_PinWrite: FIOSET = bit for 1, FIOCLR = bit for 0.
    void pin_write(unsigned port, unsigned pin, std::uint32_t value);
    // GPIO_PinRead: (FIOPIN & bit) ? 1 : 0.
    std::uint32_t pin_read(unsigned port, unsigned pin) const;

    // PIN_Configure (Keil's PIN_LPC17xx.c): read-modify-writes of the pin's two
    // PINSEL bits, its two PINMODE bits and its PINMODE_OD bit, in that order.
    void pin_configure(unsigned port, unsigned pin, unsigned function, unsigned mode, unsigned open_drain);

private:
    Lpc1768& mcu_;
};

}  // namespace latasim::lpc17xx
