// Host implementation of the Keil LPC17xx GPIO and PIN driver functions that board
// firmware calls (GPIO_LPC17xx.h, PIN_LPC17xx.h in LPC1700_DFP). C linkage, Keil's
// exact signatures, so unmodified firmware sources link against them.
//
// Each call delegates to the Phase 1 lpc17xx::KeilGpioDriver on the bound board,
// which makes the same register accesses as Keil's driver: nothing is duplicated.
// Not modeled, so accepted as no-ops: GPIO_PortClock (PCONP clock gating) and
// PIN_Configure (PINSEL/PINMODE). GPIO_PortWrite/PortRead are not provided.
#include <cstdint>
#include <exception>

#include "host/binding.hpp"
#include "lpc17xx/keil_gpio_driver.hpp"

namespace {

using latasim::host::fail;
using latasim::host::require_bound_board;
using latasim::lpc17xx::KeilGpioDriver;

// Runs `op` on the bound board's GPIO driver. Model errors (e.g. a pin out of range)
// become a fatal message: C callers cannot receive an exception.
template <class Op>
auto with_driver(const char* caller, Op op) {
    KeilGpioDriver driver(require_bound_board(caller).mcu());
    try {
        return op(driver);
    } catch (const std::exception& e) {
        fail(caller, e.what());
    }
}

}  // namespace

extern "C" {

void GPIO_PortClock(std::uint32_t /*clock*/) {
    require_bound_board("GPIO_PortClock");
}

void GPIO_SetDir(std::uint32_t port_num, std::uint32_t pin_num, std::uint32_t dir) {
    with_driver("GPIO_SetDir", [&](KeilGpioDriver& d) { d.set_dir(port_num, pin_num, dir != 0); });
}

void GPIO_PinWrite(std::uint32_t port_num, std::uint32_t pin_num, std::uint32_t val) {
    with_driver("GPIO_PinWrite", [&](KeilGpioDriver& d) { d.pin_write(port_num, pin_num, val); });
}

std::uint32_t GPIO_PinRead(std::uint32_t port_num, std::uint32_t pin_num) {
    return with_driver("GPIO_PinRead", [&](KeilGpioDriver& d) { return d.pin_read(port_num, pin_num); });
}

std::int32_t PIN_Configure(std::uint8_t /*port*/, std::uint8_t /*pin*/, std::uint8_t /*function*/,
                           std::uint8_t /*mode*/, std::uint8_t /*open_drain*/) {
    require_bound_board("PIN_Configure");
    return 0;
}

}  // extern "C"
