// Routes host-compiled register accesses (device/LPC17xx.h) to the bound board.
#include "LPC17xx.h"

#include <cstdint>
#include <exception>

#include "host/binding.hpp"
#include "lpc17xx/lpc1768.hpp"

// Address anchors only: these objects are never read or written. A proxy's LPC
// address is its offset within them, added to the peripheral's base address.
LPC_GPIO_TypeDef latasim_gpio_ports[5];
LPC_SC_TypeDef latasim_sc;
LPC_PINCON_TypeDef latasim_pincon;
LPC_TIM_TypeDef latasim_tim[4];
LPC_ADC_TypeDef latasim_adc;

namespace latasim::host {
namespace {

// Each anchor object and the LPC address its first byte stands for.
struct Window {
    const void* anchor;
    std::size_t size;
    std::uint32_t base;
};

const Window kWindows[] = {
    {latasim_gpio_ports, sizeof latasim_gpio_ports, lpc17xx::kGpioBase},
    {&latasim_sc, sizeof latasim_sc, 0x400FC000},       // LPC_SC_BASE
    {&latasim_pincon, sizeof latasim_pincon, 0x4002C000},  // LPC_PINCON_BASE
    {&latasim_tim[0], sizeof latasim_tim[0], lpc17xx::kTimerBase[0]},
    {&latasim_tim[1], sizeof latasim_tim[1], lpc17xx::kTimerBase[1]},
    {&latasim_tim[2], sizeof latasim_tim[2], lpc17xx::kTimerBase[2]},
    {&latasim_tim[3], sizeof latasim_tim[3], lpc17xx::kTimerBase[3]},
    {&latasim_adc, sizeof latasim_adc, 0x40034000},     // LPC_ADC_BASE
};

}  // namespace

std::uint32_t lpc_address(const void* proxy) {
    const auto p = reinterpret_cast<std::uintptr_t>(proxy);
    for (const Window& w : kWindows) {
        const std::uintptr_t offset = p - reinterpret_cast<std::uintptr_t>(w.anchor);  // below: wraps, rejected
        if (offset < w.size) return w.base + static_cast<std::uint32_t>(offset);
    }
    fail("register access", "not a device register (a copy of a peripheral structure?)");
}

std::uint32_t mmio_read(std::uint32_t address, unsigned size) {
    auto& mcu = require_bound_board("register read").mcu();
    try {
        switch (size) {
            case 1: return mcu.read8(address);
            case 2: return mcu.read16(address);
            default: return mcu.read32(address);
        }
    } catch (const std::exception& e) {
        fail("register read", e.what());
    }
}

void mmio_write(std::uint32_t address, unsigned size, std::uint32_t value) {
    auto& mcu = require_bound_board("register write").mcu();
    try {
        switch (size) {
            case 1: mcu.write8(address, static_cast<std::uint8_t>(value)); break;
            case 2: mcu.write16(address, static_cast<std::uint16_t>(value)); break;
            default: mcu.write32(address, value); break;
        }
    } catch (const std::exception& e) {
        fail("register write", e.what());
    }
    after_store();
}

}  // namespace latasim::host

extern "C" {

uint32_t latasim_mmio_read32(uint32_t address) { return latasim::host::mmio_read(address, 4); }
void latasim_mmio_write32(uint32_t address, uint32_t value) { latasim::host::mmio_write(address, 4, value); }

}
