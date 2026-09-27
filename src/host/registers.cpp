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

namespace latasim::host {
namespace {

constexpr std::uint32_t kScBase = 0x400FC000;  // LPC_SC_BASE

// `p - base < size` also rejects p below base (unsigned wrap).
bool inside(std::uintptr_t p, const void* object, std::size_t size, std::uintptr_t& offset) {
    offset = p - reinterpret_cast<std::uintptr_t>(object);
    return offset < size;
}

}  // namespace

std::uint32_t lpc_address(const void* proxy) {
    const auto p = reinterpret_cast<std::uintptr_t>(proxy);
    std::uintptr_t offset = 0;
    if (inside(p, latasim_gpio_ports, sizeof latasim_gpio_ports, offset))
        return lpc17xx::kGpioBase + static_cast<std::uint32_t>(offset);
    if (inside(p, &latasim_sc, sizeof latasim_sc, offset)) return kScBase + static_cast<std::uint32_t>(offset);
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
}

}  // namespace latasim::host

extern "C" {

uint32_t latasim_mmio_read32(uint32_t address) { return latasim::host::mmio_read(address, 4); }
void latasim_mmio_write32(uint32_t address, uint32_t value) { latasim::host::mmio_write(address, 4, value); }

}
