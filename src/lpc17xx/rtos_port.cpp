#include "lpc17xx/rtos_port.hpp"

namespace latasim::lpc17xx {
namespace {

constexpr std::uint32_t kStctrl = 0xE000E010;
constexpr std::uint32_t kStreload = 0xE000E014;
constexpr std::uint32_t kStcurr = 0xE000E018;
constexpr std::uint32_t kShpr3 = 0xE000ED20;

}  // namespace

void RtosPort::start_tick(std::uint32_t period, std::function<void()> tick) {
    mcu_.bind_handler(kSysTickIrq, std::move(tick));
    mcu_.write32(kStreload, period - 1);  // os_trv
    mcu_.write32(kStcurr, 0);
    mcu_.write32(kStctrl, 7);  // CPU clock, interrupt, enable
    mcu_.write32(kShpr3, mcu_.read32(kShpr3) | 0xFF000000u);
}

}  // namespace latasim::lpc17xx
