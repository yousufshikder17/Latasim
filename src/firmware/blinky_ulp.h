/* Host port of Keil's MCB1700 Blinky_ULp example (LPC1700_DFP 2.6.0,
 * Boards/Keil/MCB1700/Blinky_ULp, BSD-3-Clause): its IRQ.c is compiled unchanged;
 * blinky_ulp.c stands in for the parts that cannot run on the host
 * (docs/phase3/timed-firmware.md). */
#ifndef LATASIM_FIRMWARE_BLINKY_ULP_H
#define LATASIM_FIRMWARE_BLINKY_ULP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The start of Blinky.c's main(), up to its endless loop: LED_Initialize(),
 * ADC_Initialize(), SystemCoreClockUpdate(), SysTick_Config(SystemCoreClock/100). */
void blinky_ulp_start(void);

/* From IRQ.c: runs every 10 ms and steps the LED chase. */
void SysTick_Handler(void);

/* From IRQ.c: set once a second by SysTick_Handler. */
extern uint8_t clock_1s;

#ifdef __cplusplus
}
#endif

#endif /* LATASIM_FIRMWARE_BLINKY_ULP_H */
