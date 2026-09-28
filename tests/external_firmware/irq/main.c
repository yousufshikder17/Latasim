/* Test firmware for external interrupt handlers (external_firmware_test.cpp): a
 * 10 ms SysTick and a 1 ms TIMER0 match, whose handlers are in handlers.c. */
#include "LPC17xx.h"

int main(void) {
    LPC_GPIO1->FIODIR |= 1UL << 28;
    SysTick_Config(SystemCoreClock / 100);
    LPC_SC->PCLKSEL0 |= 1UL << 2; /* PCLK_TIMER0 = CCLK */
    LPC_TIM0->MR0 = SystemCoreClock / 1000 - 1;
    LPC_TIM0->MCR = 3;            /* interrupt and reset on MR0 */
    NVIC_EnableIRQ(TIMER0_IRQn);
    LPC_TIM0->TCR = 1;
    while (1) {
        __NOP();
    }
}
