#include "lpc17xx/gpio.hpp"

#include <stdexcept>
#include <string>

namespace vwb::lpc17xx {
namespace {

std::uint32_t bit(unsigned pin) {
    if (pin >= 32) throw std::out_of_range("GPIO pin " + std::to_string(pin) + " out of range");
    return std::uint32_t{1} << pin;
}

}  // namespace

Gpio::Gpio() {
    for (unsigned p = 0; p < kPortCount; ++p) ports_[p].external = kBondedPins[p];
}

Gpio::Port& Gpio::port_at(unsigned port) {
    if (port >= kPortCount) throw std::out_of_range("GPIO port " + std::to_string(port) + " out of range");
    return ports_[port];
}

const Gpio::Port& Gpio::port_at(unsigned port) const {
    return const_cast<Gpio*>(this)->port_at(port);
}

std::uint32_t Gpio::read(unsigned port, GpioReg reg) const {
    const Port& p = port_at(port);
    switch (reg) {
    case GpioReg::Dir: return p.dir;
    case GpioReg::Mask: return p.mask;
    case GpioReg::Pin: return p.level() & ~p.mask;
    case GpioReg::Set: return p.latch;
    case GpioReg::Clr: return 0;
    }
    throw std::invalid_argument("unknown GPIO register");
}

void Gpio::write(unsigned port, GpioReg reg, std::uint32_t value) {
    Port& p = port_at(port);
    const std::uint32_t writable = value & ~p.mask;
    switch (reg) {
    case GpioReg::Dir: p.dir = value; return;
    case GpioReg::Mask: p.mask = value; return;
    case GpioReg::Pin: p.latch = (p.latch & p.mask) | writable; return;
    case GpioReg::Set: p.latch |= writable; return;
    case GpioReg::Clr: p.latch &= ~writable; return;
    }
    throw std::invalid_argument("unknown GPIO register");
}

void Gpio::set_external_level(unsigned port, unsigned pin, bool high) {
    Port& p = port_at(port);
    p.external = high ? (p.external | bit(pin)) : (p.external & ~bit(pin));
}

bool Gpio::pin_level(unsigned port, unsigned pin) const {
    return (port_at(port).level() & bit(pin)) != 0;
}

bool Gpio::is_output(unsigned port, unsigned pin) const {
    return (port_at(port).dir & bit(pin)) != 0;
}

}  // namespace vwb::lpc17xx
