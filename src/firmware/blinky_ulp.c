/* Host stand-ins for the parts of Keil's Blinky_ULp that IRQ.c needs but that
 * cannot run on the host. See blinky_ulp.h and docs/phase3/timed-firmware.md. */
#include "firmware/blinky_ulp.h"

#include "Board_ADC.h"
#include "Board_LED.h"
#include "LPC17xx.h"

/* Defined in Blinky.c, which is not compiled: its main() never returns. */
uint16_t AD_last;

void blinky_ulp_start(void) {
    LED_Initialize();
    ADC_Initialize();
    SystemCoreClockUpdate();
    SysTick_Config(SystemCoreClock / 100); /* Generate interrupt each 10 ms */
}

/* Board_ADC.h without an A/D converter: Latasim does not model the ADC, so no
 * conversion ever completes and AD_last keeps its initial 0. With AD_last = 0,
 * IRQ.c steps the LED chase on every SysTick. */
int32_t ADC_Initialize(void) { return 0; }
int32_t ADC_Uninitialize(void) { return 0; }
int32_t ADC_StartConversion(void) { return 0; }
int32_t ADC_ConversionDone(void) { return -1; }
int32_t ADC_GetValue(void) { return -1; }
uint32_t ADC_GetResolution(void) { return 12; }
