// Representative EINT0 firmware, compiled against the host LPC17xx.h. The Keil
// packs have no EINT0 example; this follows UM10360 section 3.6's procedure: select
// the pin's EINT0 function, set mode and polarity while the interrupt is disabled,
// clear EXTINT, then enable it in the NVIC; the handler clears its flag by writing
// 1. Each interrupt counts a press and toggles LED7 (P2.6).
#include "LPC17xx.h"

#include "host/eint0_counter.h"

volatile uint32_t eint0_count;

extern "C" void EINT0_IRQHandler(void) {
    LPC_SC->EXTINT = 1U;  // clear EINT0 (only takes effect once the pin is inactive in level mode)
    if (++eint0_count & 1U) {
        LPC_GPIO2->FIOSET = 1UL << 6;
    } else {
        LPC_GPIO2->FIOCLR = 1UL << 6;
    }
}

void eint0_counter_start(bool edge, bool rising_or_high) {
    eint0_count = 0;
    LPC_GPIO2->FIODIR |= 1UL << 6;
    LPC_PINCON->PINSEL4 = (LPC_PINCON->PINSEL4 & ~(3UL << 20)) | (1UL << 20);  // P2.10 = EINT0
    NVIC_DisableIRQ(EINT0_IRQn);
    LPC_SC->EXTMODE = edge ? (LPC_SC->EXTMODE | 1U) : (LPC_SC->EXTMODE & ~1U);
    LPC_SC->EXTPOLAR = rising_or_high ? (LPC_SC->EXTPOLAR | 1U) : (LPC_SC->EXTPOLAR & ~1U);
    LPC_SC->EXTINT = 1U;
    NVIC_EnableIRQ(EINT0_IRQn);
}
