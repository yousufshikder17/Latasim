/* E13: Phase 4 peripherals in the LPC1768 simulator, exercised by firmware.
 * TIMER1 runs free at CCLK as a timestamp; each r[] slot records one value and the
 * INI prints them all. Polling loops add a few clocks of latency to timestamps. */
#include "LPC17xx.h"

volatile uint32_t r[40];
volatile uint32_t order[8];
volatile uint32_t n_order;
volatile uint32_t eint_count;
volatile unsigned int g_id;

__attribute__((noinline)) void finished(void) { g_id = 0xFF; }
__attribute__((noinline)) void wait_pin(void) { g_id = 1; }

void TIMER0_IRQHandler(void) { LPC_TIM0->IR = 1; order[n_order++] = 17; }
void TIMER2_IRQHandler(void) { LPC_TIM2->IR = 1; order[n_order++] = 19; }
void SysTick_Handler(void) { order[n_order++] = 15; }
void EINT0_IRQHandler(void) { LPC_SC->EXTINT = 1; ++eint_count; }

static void timer_reset(LPC_TIM_TypeDef *t) { t->TCR = 2; t->IR = 0x3F; t->PR = 0; t->MCR = 0; }

int main(void) {
    int i;
    uint32_t t0;
    __disable_irq();
    LPC_SC->PCONP |= (1UL << 1) | (1UL << 2) | (1UL << 22) | (1UL << 12);  /* TIM0/1/2, ADC */
    LPC_SC->PCLKSEL0 = (LPC_SC->PCLKSEL0 & ~((3UL << 2) | (3UL << 4) | (3UL << 24))) | (1UL << 2) | (1UL << 4);
    LPC_SC->PCLKSEL1 = (LPC_SC->PCLKSEL1 & ~(3UL << 12)) | (1UL << 12);  /* TIM2 at CCLK */
    timer_reset(LPC_TIM1);
    LPC_TIM1->TCR = 1;                                        /* timestamp clock */

    /* A: IR at the match, interrupt flag only; TC read right after the flag. */
    timer_reset(LPC_TIM0);
    LPC_TIM0->MR0 = 1000; LPC_TIM0->MCR = 1; LPC_TIM0->TCR = 1;
    while (!(LPC_TIM0->IR & 1)) {}
    r[0] = LPC_TIM0->TC;                                      /* 1000 + poll latency */
    /* same, with reset on match */
    timer_reset(LPC_TIM0);
    LPC_TIM0->MR0 = 1000; LPC_TIM0->MCR = 3; LPC_TIM0->TCR = 1;
    while (!(LPC_TIM0->IR & 1)) {}
    r[1] = LPC_TIM0->TC;                                      /* model: r0 - 1000 - 1 */
    /* period over 10 matches with reset, timed by TIMER1 */
    LPC_TIM0->IR = 1;
    while (!(LPC_TIM0->IR & 1)) {}
    LPC_TIM0->IR = 1;
    t0 = LPC_TIM1->TC;
    for (i = 0; i < 10; ++i) { while (!(LPC_TIM0->IR & 1)) {} LPC_TIM0->IR = 1; }
    r[2] = LPC_TIM1->TC - t0;                                 /* model: 10 * 1001 */
    /* with prescale 3 */
    timer_reset(LPC_TIM0);
    LPC_TIM0->PR = 3; LPC_TIM0->MR0 = 100; LPC_TIM0->MCR = 3; LPC_TIM0->TCR = 1;
    while (!(LPC_TIM0->IR & 1)) {}
    LPC_TIM0->IR = 1;
    t0 = LPC_TIM1->TC;
    for (i = 0; i < 10; ++i) { while (!(LPC_TIM0->IR & 1)) {} LPC_TIM0->IR = 1; }
    r[3] = LPC_TIM1->TC - t0;                                 /* model: 10 * 101 * 4 */
    /* stop on match: TC holds at MR0 */
    timer_reset(LPC_TIM0);
    LPC_TIM0->MR0 = 50; LPC_TIM0->MCR = 4; LPC_TIM0->TCR = 1;
    for (i = 0; i < 100; ++i) {}
    r[4] = LPC_TIM0->TC;
    r[5] = LPC_TIM0->TCR;

    /* B: a level line re-pends after ICPR while still asserted. */
    timer_reset(LPC_TIM0);
    LPC_TIM0->MR0 = 10; LPC_TIM0->MCR = 1; LPC_TIM0->TCR = 1;
    while (!(LPC_TIM0->IR & 1)) {}
    LPC_TIM0->TCR = 0;
    r[6] = NVIC->ISPR[0];                                     /* bit 1: pending while disabled */
    NVIC->ICPR[0] = 1UL << 1;
    r[7] = NVIC->ISPR[0];                                     /* model: bit 1 set again */
    LPC_TIM0->IR = 1;
    NVIC->ICPR[0] = 1UL << 1;
    r[8] = NVIC->ISPR[0];                                     /* cleared once IR is */

    /* C: arbitration with PRIMASK set, then released. */
    NVIC_SetPriority(TIMER0_IRQn, 0);
    NVIC_SetPriority(SysTick_IRQn, 31);
    NVIC_SetPriority(TIMER2_IRQn, 0);
    NVIC_EnableIRQ(TIMER0_IRQn);
    NVIC_EnableIRQ(TIMER2_IRQn);
    SCB->ICSR = SCB_ICSR_PENDSTSET_Msk;                       /* SysTick first... */
    NVIC->ISPR[0] = 1UL << 3;                                 /* ...then TIMER2... */
    NVIC->ISPR[0] = 1UL << 1;                                 /* ...then TIMER0 */
    __enable_irq();
    __disable_irq();
    r[9] = n_order;
    r[10] = order[0]; r[11] = order[1]; r[12] = order[2];     /* model: 17, 19, 15 */
    r[13] = NVIC_GetPriority(TIMER0_IRQn);
    NVIC_SetPriority(TIMER0_IRQn, 0xFF);
    r[14] = NVIC->IP[1];                                      /* model: 0xF8 */

    /* D: ADC, Keil's settings: PCLK_ADC = CCLK/4, CLKDIV 4, AD0.2 on P0.25. */
    LPC_PINCON->PINSEL1 = (LPC_PINCON->PINSEL1 & ~(3UL << 18)) | (1UL << 18);
    LPC_ADC->ADCR = (1UL << 2) | (4UL << 8) | (1UL << 21);
    t0 = LPC_TIM1->TC;
    LPC_ADC->ADCR |= 1UL << 24;
    while (!(LPC_ADC->ADGDR & (1UL << 31))) {}
    r[15] = LPC_TIM1->TC - t0;                                /* model: 1300 + latency */
    r[16] = LPC_ADC->ADGDR;                                   /* DONE cleared by the read above? */
    r[17] = LPC_ADC->ADDR2;                                   /* channel DONE still set */
    r[18] = LPC_ADC->ADDR2;                                   /* cleared by the read */
    r[19] = LPC_ADC->ADSTAT;
    LPC_ADC->ADCR |= 1UL << 24;
    while (!(LPC_ADC->ADSTAT & (1UL << 2))) {}
    r[20] = LPC_ADC->ADSTAT;                                  /* bit 16: interrupt line */
    LPC_ADC->ADCR = (1UL << 2) | (4UL << 8) | (1UL << 21);    /* ADCR write: global DONE */
    r[21] = LPC_ADC->ADSTAT;
    r[22] = LPC_ADC->ADINTEN;                                 /* reset value */

    /* E: EINT0, falling edge, driven from the INI through PORT2. */
    LPC_PINCON->PINSEL4 = (LPC_PINCON->PINSEL4 & ~(3UL << 20)) | (1UL << 20);
    LPC_SC->EXTMODE = 1; LPC_SC->EXTPOLAR = 0; LPC_SC->EXTINT = 1;
    NVIC_EnableIRQ(EINT0_IRQn);
    __enable_irq();
    r[23] = LPC_SC->EXTINT;
    wait_pin();                                               /* INI: P2.10 low */
    for (i = 0; i < 100; ++i) {}
    r[24] = eint_count;
    r[25] = LPC_SC->EXTINT;
    wait_pin();                                               /* INI: P2.10 high */
    for (i = 0; i < 100; ++i) {}
    r[26] = eint_count;                                       /* rising edge: no interrupt */
    wait_pin();                                               /* INI: P2.10 low */
    for (i = 0; i < 100; ++i) {}
    r[27] = eint_count;
    /* level mode, low, pin held low: the flag cannot be cleared */
    __disable_irq();
    LPC_SC->EXTMODE = 0;
    LPC_SC->EXTINT = 1;
    r[28] = LPC_SC->EXTINT;
    finished();
    for (;;) {}
}
