/* Test firmware for Keil board drivers in external firmware
 * (external_firmware_test.cpp): Keil's ADC driver, started every 10 ms from
 * SysTick and read by a polling main loop, as Keil's Blinky examples do. The
 * driver's own ADC_IRQHandler completes each conversion. */
#include "LPC17xx.h"
#include "Board_ADC.h"

volatile long adc_sample_last = -1;
volatile unsigned long adc_sample_passes;

void SysTick_Handler(void) { ADC_StartConversion(); }

int main(void) {
    int32_t res;
    ADC_Initialize();
    SysTick_Config(SystemCoreClock / 100);
    while (1) {
        res = ADC_GetValue();
        if (res != -1) adc_sample_last = res;
        adc_sample_passes++;
        __NOP();
    }
}
