#include "lpc17xx/keil_gpio_driver.hpp"

namespace vwb::lpc17xx {

void KeilGpioDriver::set_dir(unsigned port, unsigned pin, bool output) {
    const std::uint32_t dir = gpio_register_address(port, GpioReg::Dir);
    const std::uint32_t bit = std::uint32_t{1} << pin;
    const std::uint32_t value = mcu_.read32(dir);
    mcu_.write32(dir, output ? (value | bit) : (value & ~bit));
}

void KeilGpioDriver::pin_write(unsigned port, unsigned pin, std::uint32_t value) {
    mcu_.write32(gpio_register_address(port, value ? GpioReg::Set : GpioReg::Clr), std::uint32_t{1} << pin);
}

std::uint32_t KeilGpioDriver::pin_read(unsigned port, unsigned pin) const {
    return (mcu_.read32(gpio_register_address(port, GpioReg::Pin)) >> pin) & 1u;
}

}  // namespace vwb::lpc17xx
