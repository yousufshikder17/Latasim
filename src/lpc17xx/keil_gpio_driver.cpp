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

void KeilGpioDriver::pin_configure(unsigned port, unsigned pin, unsigned function, unsigned mode,
                                   unsigned open_drain) {
    pin_bit(pin);  // range check, as the GPIO functions
    const std::uint32_t regidx = pin >= 16 ? 1u : 0u;
    const unsigned shift = (pin % 16) * 2;
    const std::uint32_t sel = kPinconBase + 4 * (2 * port + regidx);           // PINSELn
    const std::uint32_t mod = kPinconBase + 0x40 + 4 * (2 * port + regidx);    // PINMODEn
    const std::uint32_t od = kPinconBase + 0x68 + 4 * port;                    // PINMODE_ODn
    mcu_.write32(sel, mcu_.read32(sel) & ~(3u << shift));
    mcu_.write32(sel, mcu_.read32(sel) | ((function & 0xFFu) << shift));
    mcu_.write32(mod, mcu_.read32(mod) & ~(3u << shift));
    mcu_.write32(mod, mcu_.read32(mod) | ((mode & 0xFFu) << shift));
    if (open_drain == 1) mcu_.write32(od, mcu_.read32(od) | (1u << pin));  // PIN_PINMODE_OPENDRAIN
    else mcu_.write32(od, mcu_.read32(od) & ~(1u << pin));
}

}  // namespace latasim::lpc17xx
