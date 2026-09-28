// Host versions of the CMSIS system and core functions firmware calls (declared in
// device/LPC17xx.h). On the target they come from system_LPC17xx.c and core_cm3.h,
// which need the real clock registers and compiler intrinsics. Here the core_cm3.h
// ones make the same register accesses, through the bound board's MMIO, so the
// NVIC and SysTick registers stay the only interrupt state.
#include <cstdint>

#include "LPC17xx.h"
#include "host/registers.hpp"
#include "lpc17xx/lpc1768.hpp"
#include "lpc17xx/nvic.hpp"
#include "lpc17xx/systick.hpp"

namespace {

using namespace latasim::lpc17xx;
using latasim::host::mmio_read;
using latasim::host::mmio_write;

constexpr std::uint32_t address(SysTickReg reg) { return kSysTickBase + static_cast<std::uint32_t>(reg); }

// core_cm3.h: NVIC->ISER[n] etc. are 32-bit bitmaps of IRQn >> 5, bit IRQn & 31.
constexpr std::uint32_t kIser = 0xE000E100;
constexpr std::uint32_t kIcer = 0xE000E180;
constexpr std::uint32_t kIspr = 0xE000E200;
constexpr std::uint32_t kIcpr = 0xE000E280;
constexpr std::uint32_t kIabr = 0xE000E300;
constexpr std::uint32_t kIp = 0xE000E400;   // NVIC->IP[IRQn], one byte each
constexpr std::uint32_t kShp = 0xE000ED18;  // SCB->SHP[(IRQn & 0xF) - 4]

std::uint32_t bitmap(std::uint32_t base, int irq) { return base + 4u * (static_cast<std::uint32_t>(irq) >> 5); }
std::uint32_t bit(IRQn_Type irq) { return 1u << (static_cast<std::uint32_t>(irq) & 0x1Fu); }
std::uint32_t priority_address(IRQn_Type irq) {
    return irq >= 0 ? kIp + static_cast<std::uint32_t>(irq) : kShp + ((static_cast<std::uint32_t>(irq) & 0xFu) - 4u);
}

}  // namespace

extern "C" {

// The core clock Latasim's virtual time runs at (lpc17xx::kCoreClockHz). There is
// no clock tree to read back, so SystemCoreClockUpdate has nothing to update.
uint32_t SystemCoreClock = static_cast<uint32_t>(kCoreClockHz);
void SystemCoreClockUpdate(void) {}

// As CMSIS core_cm3.h: negative (core) IRQn are ignored by the enable/pending
// functions, and their priorities live in SCB->SHP.
void NVIC_EnableIRQ(IRQn_Type irq) {
    if (irq >= 0) mmio_write(bitmap(kIser, irq), 4, bit(irq));
}
void NVIC_DisableIRQ(IRQn_Type irq) {
    if (irq >= 0) mmio_write(bitmap(kIcer, irq), 4, bit(irq));
}
void NVIC_SetPendingIRQ(IRQn_Type irq) {
    if (irq >= 0) mmio_write(bitmap(kIspr, irq), 4, bit(irq));
}
void NVIC_ClearPendingIRQ(IRQn_Type irq) {
    if (irq >= 0) mmio_write(bitmap(kIcpr, irq), 4, bit(irq));
}
uint32_t NVIC_GetPendingIRQ(IRQn_Type irq) {
    return irq >= 0 && (mmio_read(bitmap(kIspr, irq), 4) & bit(irq)) ? 1u : 0u;
}
uint32_t NVIC_GetActive(IRQn_Type irq) { return irq >= 0 && (mmio_read(bitmap(kIabr, irq), 4) & bit(irq)) ? 1u : 0u; }
void NVIC_SetPriority(IRQn_Type irq, uint32_t priority) {
    mmio_write(priority_address(irq), 1, (priority << (8u - kPriorityBits)) & 0xFFu);
}
uint32_t NVIC_GetPriority(IRQn_Type irq) { return mmio_read(priority_address(irq), 1) >> (8u - kPriorityBits); }

// CMSIS core_cm3.h SysTick_Config: RELOAD, the lowest SysTick priority, counter
// cleared, then run with the core clock and TICKINT.
uint32_t SysTick_Config(uint32_t ticks) {
    if (ticks - 1u > kSysTickCounterMask) return 1u;  // reload value impossible
    mmio_write(address(SysTickReg::Load), 4, ticks - 1u);
    NVIC_SetPriority(SysTick_IRQn, (1u << kPriorityBits) - 1u);
    mmio_write(address(SysTickReg::Val), 4, 0);
    mmio_write(address(SysTickReg::Ctrl), 4, kSysTickClksource | kSysTickTickint | kSysTickEnable);
    return 0u;
}

}
