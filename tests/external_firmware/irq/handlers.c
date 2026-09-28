/* The interrupt handlers of irq/main.c, in a file main.c does not reference. */
#include "LPC17xx.h"

volatile unsigned long irq_sample_ticks;
volatile unsigned long irq_sample_matches;

void SysTick_Handler(void) {
    if (++irq_sample_ticks & 1) {
        LPC_GPIO1->FIOSET = 1UL << 28;
    } else {
        LPC_GPIO1->FIOCLR = 1UL << 28;
    }
}

void TIMER0_IRQHandler(void) {
    LPC_TIM0->IR = 1;
    irq_sample_matches++;
}
