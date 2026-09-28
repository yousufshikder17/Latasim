// Representative TIMER0 firmware (timer0_blink.cpp).
#pragma once
#include <cstdint>

extern "C" void TIMER0_IRQHandler(void);
extern volatile std::uint32_t timer0_blink_ticks;

// Starts TIMER0 at CCLK, interrupting `frequency` times a second; each interrupt
// toggles LED0.
void timer0_blink_start(std::uint32_t frequency);
