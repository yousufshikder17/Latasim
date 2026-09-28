#pragma once
// The RTOS kernel's view of an LPC1768 (rtos::Platform): virtual time, the SysTick
// as the kernel tick, and the trace. Like RTX's HAL_CM, it programs SysTick through
// the memory map (rt_systick_init: STRELOAD, STCURR, STCTRL = 7, SysTick priority
// lowest), so those stores appear in the trace as they would from RTX, and the
// kernel's post-interrupt work runs where RTX's PendSV handler does: when the
// processor returns to thread mode.
#include "lpc17xx/lpc1768.hpp"
#include "rtos/kernel.hpp"

namespace latasim::lpc17xx {

class RtosPort : public rtos::Platform {
public:
    explicit RtosPort(Lpc1768& mcu) : mcu_(mcu) {}

    std::uint64_t now() const override { return mcu_.cycles(); }
    std::uint64_t cycles_to_next_event() const override { return mcu_.cycles_to_next_event(); }
    void advance(std::uint64_t cycles) override { mcu_.advance_cycles(cycles); }
    void start_tick(std::uint32_t period, std::function<void()> tick) override;
    void on_interrupts_done(std::function<void()> hook) override { mcu_.on_thread_mode(std::move(hook)); }
    bool in_interrupt() const override { return mcu_.in_handler(); }
    void record(TraceEvent event) override { mcu_.trace().record(event, mcu_.cycles()); }

private:
    Lpc1768& mcu_;
};

}  // namespace latasim::lpc17xx
