// Representative register-level timer firmware, compiled against the host
// LPC17xx.h. No Keil MCB1700 example uses a timer on its own (the only one is the
// USB audio example's playback clock, which needs USB); this follows that
// example's TIMER0 set-up and handler (Boards/Keil/MCB1700/Middleware/USB/Device/
// Audio/USBD_User_ADC_0.c: MR0 = SystemCoreClock/f - 1, MCR = 3, TCR = 3 then 1,
// IR = 1 in the handler), toggling LED0 (P1.28) on each interrupt.
#include "LPC17xx.h"

#include "host/timer0_blink.h"

volatile uint32_t timer0_blink_ticks;

extern "C" void TIMER0_IRQHandler(void) {
    LPC_TIM0->IR = 1U;  // clear the MR0 interrupt
    if (++timer0_blink_ticks & 1U) {
        LPC_GPIO1->FIOSET = 1UL << 28;
    } else {
        LPC_GPIO1->FIOCLR = 1UL << 28;
    }
}

void timer0_blink_start(uint32_t frequency) {
    timer0_blink_ticks = 0;
    LPC_SC->PCONP |= 1UL << 1;                                    // PCTIM0
    LPC_SC->PCLKSEL0 = (LPC_SC->PCLKSEL0 & ~(3UL << 2)) | (1UL << 2);  // PCLK_TIMER0 = CCLK
    LPC_GPIO1->FIODIR |= 1UL << 28;
    LPC_TIM0->MR0 = (SystemCoreClock / frequency) - 1U;
    LPC_TIM0->MCR = 3U;  // interrupt and reset on MR0
    NVIC_EnableIRQ(TIMER0_IRQn);
    LPC_TIM0->TCR = 3U;  // reset
    LPC_TIM0->TCR = 1U;  // enable
}
