/* Latasim host stand-in for the LPC17xx device header.
 *
 * Put this directory ahead of the real device header when compiling firmware for
 * the host. The real LPC17xx.h pulls in core_cm3.h, whose compiler intrinsics the
 * host compiler cannot build, and defines peripherals as pointers to fixed ARM
 * addresses, which are not host memory.
 *
 * Firmware that only calls driver/board APIs (GPIO_SetDir, LED_On, ...) needs
 * nothing from this header beyond <stdint.h>. */
#ifndef LATASIM_HOST_LPC17XX_H
#define LATASIM_HOST_LPC17XX_H

#include <stdint.h>

#endif /* LATASIM_HOST_LPC17XX_H */
