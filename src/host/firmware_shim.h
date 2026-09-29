/* Force-included ahead of every source of host-compiled external firmware
 * (latasim_add_host_firmware in CMakeLists.txt, docs/external-firmware.md). The
 * sources are compiled as C++ against the host LPC17xx.h.
 *
 * - LATASIM_HOST is defined, for the few source adaptations a host build needs
 *   (literal register addresses: LATASIM_REG32, LATASIM_REG32_PTR).
 * - Each source is compiled inside the firmware's own namespace,
 *   LATASIM_FIRMWARE_NAMESPACE (a generated wrapper includes it there), so two
 *   firmware in one executable can both define GLCD_Init or LED_Init. Only the
 *   functions below have C linkage and are shared by name.
 * - main() is renamed LATASIM_FIRMWARE_MAIN (a compile definition, with C linkage):
 *   the workbench runs it as a bare-metal scenario's main (workbench/session.hpp).
 * - __NOP() consumes LATASIM_NOP_CYCLES core cycles of virtual time: a busy-wait
 *   delay loop around it (volatile counter plus __NOP) takes the time it would
 *   take on the chip, whose simulator measured such a loop at 10 cycles per pass.
 *   Loops without a __NOP still take no virtual time.
 * - fputc is renamed LATASIM_FIRMWARE_FPUTC, out of the C library's way. Firmware that retargets printf
 *   to ITM by defining fputc keeps its definition, unused: printf writes to the
 *   host process's standard output. ITM is not modelled.
 * - The CMSIS interrupt handlers are renamed per firmware (below).
 * - Keil board-support headers (Board_*.h) of the board drivers the firmware uses
 *   are seen through wrappers that give them C linkage (latasim_add_host_firmware).
 */
#ifndef LATASIM_FIRMWARE_SHIM_H
#define LATASIM_FIRMWARE_SHIM_H

/* The C library headers firmware commonly uses, included here at global scope so
 * that a firmware's own #include of them, inside its namespace, adds nothing. */
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cstdio>

#include "latasim_rtos.h"

#define LATASIM_HOST 1
#define __NOP() latasim_consume_cycles(LATASIM_NOP_CYCLES)

#include "LPC17xx.h"

#define main LATASIM_FIRMWARE_MAIN
#define fputc LATASIM_FIRMWARE_FPUTC

/* The CMSIS interrupt handlers of the exceptions Latasim models are renamed
 * <LATASIM_FIRMWARE_MAIN>_<name>, with C linkage, so they cannot collide with
 * another firmware's (a built-in scenario's SysTick_Handler). The table that
 * latasim_add_host_firmware generates binds the ones the firmware defines. */
#define LATASIM_ISR_NAME2(entry, name) entry##_##name
#define LATASIM_ISR_NAME(entry, name) LATASIM_ISR_NAME2(entry, name)
#define SysTick_Handler LATASIM_ISR_NAME(LATASIM_FIRMWARE_MAIN, SysTick_Handler)
#define TIMER0_IRQHandler LATASIM_ISR_NAME(LATASIM_FIRMWARE_MAIN, TIMER0_IRQHandler)
#define TIMER1_IRQHandler LATASIM_ISR_NAME(LATASIM_FIRMWARE_MAIN, TIMER1_IRQHandler)
#define TIMER2_IRQHandler LATASIM_ISR_NAME(LATASIM_FIRMWARE_MAIN, TIMER2_IRQHandler)
#define TIMER3_IRQHandler LATASIM_ISR_NAME(LATASIM_FIRMWARE_MAIN, TIMER3_IRQHandler)
#define EINT0_IRQHandler LATASIM_ISR_NAME(LATASIM_FIRMWARE_MAIN, EINT0_IRQHandler)
#define ADC_IRQHandler LATASIM_ISR_NAME(LATASIM_FIRMWARE_MAIN, ADC_IRQHandler)
#define USB_IRQHandler LATASIM_ISR_NAME(LATASIM_FIRMWARE_MAIN, USB_IRQHandler)
/* Declared with C linkage in the firmware's namespace, where its definitions are. */
namespace LATASIM_FIRMWARE_NAMESPACE {
extern "C" {
int main(void);
void SysTick_Handler(void);
void TIMER0_IRQHandler(void);
void TIMER1_IRQHandler(void);
void TIMER2_IRQHandler(void);
void TIMER3_IRQHandler(void);
void EINT0_IRQHandler(void);
void ADC_IRQHandler(void);
void USB_IRQHandler(void);
}
}

#endif /* LATASIM_FIRMWARE_SHIM_H */
