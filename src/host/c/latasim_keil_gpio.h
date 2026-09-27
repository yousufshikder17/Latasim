/* C declarations of the Keil LPC17xx GPIO/PIN driver functions that Latasim
 * provides on the host (src/host/keil_rte_gpio.cpp). They match Keil's
 * GPIO_LPC17xx.h and PIN_LPC17xx.h (LPC1700_DFP, Apache-2.0), so firmware can use
 * either header. Only the functions Latasim implements are declared. */
#ifndef LATASIM_KEIL_GPIO_H
#define LATASIM_KEIL_GPIO_H

#include <stdint.h>

#define GPIO_DIR_INPUT  (0U)
#define GPIO_DIR_OUTPUT (1U)

#ifdef __cplusplus
extern "C" {
#endif

void GPIO_PortClock(uint32_t clock);
void GPIO_SetDir(uint32_t port_num, uint32_t pin_num, uint32_t dir);
void GPIO_PinWrite(uint32_t port_num, uint32_t pin_num, uint32_t val);
uint32_t GPIO_PinRead(uint32_t port_num, uint32_t pin_num);
int32_t PIN_Configure(uint8_t port, uint8_t pin, uint8_t function, uint8_t mode, uint8_t open_drain);

#ifdef __cplusplus
}
#endif

#endif /* LATASIM_KEIL_GPIO_H */
