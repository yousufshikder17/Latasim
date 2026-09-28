/* Host port of Keil's MCB1700 Blinky_ULp example (LPC1700_DFP 2.6.0,
 * Boards/Keil/MCB1700/Blinky_ULp, BSD-3-Clause). IRQ.c and the board's
 * ADC_MCB1700.c are compiled unchanged; blinky_ulp.c stands in for Blinky.c,
 * whose main() never returns (docs/phase4/overview.md).
 *
 * To run it: blinky_ulp_start(), then bind SysTick_Handler and ADC_IRQHandler to
 * their IRQs and blinky_ulp_main_loop_step as the thread-mode step, then advance
 * virtual time. */
#ifndef LATASIM_FIRMWARE_BLINKY_ULP_H
#define LATASIM_FIRMWARE_BLINKY_ULP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The start of Blinky.c's main(), up to its endless loop: LED_Initialize(),
 * ADC_Initialize(), SystemCoreClockUpdate(), SysTick_Config(SystemCoreClock/100). */
void blinky_ulp_start(void);

/* What Blinky.c's endless loop does with a finished conversion: its first two
 * statements, "res = ADC_GetValue(); if (res != -1) AD_last = res;". The rest of
 * the loop only averages the value for display and prints once a second. */
void blinky_ulp_main_loop_step(void);

/* From IRQ.c: runs every 10 ms, starts an ADC conversion and steps the LED chase
 * every (AD_last >> 8) + 1 ticks. */
void SysTick_Handler(void);

/* From ADC_MCB1700.c: stores the conversion result. */
void ADC_IRQHandler(void);

/* From IRQ.c: set once a second by SysTick_Handler. */
extern uint8_t clock_1s;

/* Blinky.c's copy of the last converted value, which sets the chase speed. */
extern uint16_t AD_last;

#ifdef __cplusplus
}
#endif

#endif /* LATASIM_FIRMWARE_BLINKY_ULP_H */
