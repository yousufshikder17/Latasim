#include "lpc17xx/keil_gpio_driver.hpp"

#include <stdexcept>
#include <string>

namespace latasim::lpc17xx {
namespace {

// Keil's driver shifts by pin_num unchecked; pin >= 32 would be undefined here.
std::uint32_t pin_bit(unsigned pin) {
    if (pin >= 32) throw std::out_of_range("GPIO pin " + std::to_string(pin) + " out of range");
    return std::uint32_t{1} << pin;
}

}  // namespace

void KeilGpioDriver::port_clock(bool on) {
    const std::uint32_t pconp = mcu_.read32(kPconpAddress);
    mcu_.write32(kPconpAddress, on ? (pconp | kPconpGpio) : (pconp & ~kPconpGpio));
}

void KeilGpioDriver::set_dir(unsigned port, unsigned pin, bool output) {
    const std::uint32_t bit = pin_bit(pin);
    const std::uint32_t dir = gpio_register_address(port, GpioReg::Dir);
    const std::uint32_t value = mcu_.read32(dir);
    mcu_.write32(dir, output ? (value | bit) : (value & ~bit));
}

void KeilGpioDriver::pin_write(unsigned port, unsigned pin, std::uint32_t value) {
    mcu_.write32(gpio_register_address(port, value ? GpioReg::Set : GpioReg::Clr), pin_bit(pin));
}

std::uint32_t KeilGpioDriver::pin_read(unsigned port, unsigned pin) const {
    const std::uint32_t bit = pin_bit(pin);
    return (mcu_.read32(gpio_register_address(port, GpioReg::Pin)) & bit) ? 1u : 0u;
}

}  // namespace latasim::lpc17xx
