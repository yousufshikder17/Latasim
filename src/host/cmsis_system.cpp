// Host versions of the CMSIS system and core functions timed firmware calls
// (declared in device/LPC17xx.h). On the target they come from system_LPC17xx.c
// and core_cm3.h, which need the real clock registers and compiler intrinsics.
#include <cstdint>

#include "host/registers.hpp"
#include "lpc17xx/lpc1768.hpp"
#include "lpc17xx/systick.hpp"

namespace {

using namespace latasim::lpc17xx;

constexpr std::uint32_t address(SysTickReg reg) { return kSysTickBase + static_cast<std::uint32_t>(reg); }

}  // namespace

extern "C" {

// The core clock Latasim's virtual time runs at (lpc17xx::kCoreClockHz). There is
// no clock tree to read back, so SystemCoreClockUpdate has nothing to update.
uint32_t SystemCoreClock = static_cast<uint32_t>(kCoreClockHz);
void SystemCoreClockUpdate(void) {}

// CMSIS core_cm3.h SysTick_Config, through the bound board's MMIO, minus its
// NVIC_SetPriority(SysTick_IRQn, ...): exception priorities are not modeled, and
// SysTick is the only exception Latasim delivers.
uint32_t SysTick_Config(uint32_t ticks) {
    if (ticks - 1u > kSysTickCounterMask) return 1u;  // reload value impossible
    latasim::host::mmio_write(address(SysTickReg::Load), 4, ticks - 1u);
    latasim::host::mmio_write(address(SysTickReg::Val), 4, 0);
    latasim::host::mmio_write(address(SysTickReg::Ctrl), 4, kSysTickClksource | kSysTickTickint | kSysTickEnable);
    return 0u;
}

}
