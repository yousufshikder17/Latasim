// Representative EINT0 firmware (eint0_counter.cpp).
#pragma once
#include <cstdint>

extern "C" void EINT0_IRQHandler(void);
extern volatile std::uint32_t eint0_count;

// Configures P2.10 as EINT0, edge- or level-sensitive, falling/low or rising/high.
void eint0_counter_start(bool edge, bool rising_or_high);
