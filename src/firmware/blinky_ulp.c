/* Host stand-ins for the parts of Keil's Blinky_ULp that cannot run on the host as
 * they are. See blinky_ulp.h and docs/phase4/overview.md. */
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

void blinky_ulp_main_loop_step(void) {
    int32_t res = ADC_GetValue();
    if (res != -1) { /* If conversion has finished */
        AD_last = (uint16_t)res;
    }
}
