/* Force-included ahead of every source of host-compiled external firmware
 * (latasim_add_host_firmware in CMakeLists.txt, docs/external-firmware.md). The
 * sources are compiled as C++ against the host LPC17xx.h.
 *
 * - LATASIM_HOST is defined, for the few source adaptations a host build needs
 *   (literal register addresses: LATASIM_REG32, LATASIM_REG32_PTR).
 * - main() is renamed LATASIM_FIRMWARE_MAIN (a compile definition, with C linkage):
 *   the workbench runs it as a bare-metal scenario's main (workbench/session.hpp).
 * - __NOP() consumes LATASIM_NOP_CYCLES core cycles of virtual time: a busy-wait
 *   delay loop around it (volatile counter plus __NOP) takes the time it would
 *   take on the chip, whose simulator measured such a loop at 10 cycles per pass.
 *   Loops without a __NOP still take no virtual time.
 * - fputc is renamed LATASIM_FIRMWARE_FPUTC, out of the C library's way. Firmware that retargets printf
 *   to ITM by defining fputc keeps its definition, unused: printf writes to the
 *   host process's standard output. ITM is not modelled.
 */
#ifndef LATASIM_FIRMWARE_SHIM_H
#define LATASIM_FIRMWARE_SHIM_H

#include <stdio.h>

#include <cstdio>

#include "latasim_rtos.h"

#define LATASIM_HOST 1
#define __NOP() latasim_consume_cycles(LATASIM_NOP_CYCLES)

#include "LPC17xx.h"

extern "C" int LATASIM_FIRMWARE_MAIN(void);
#define main LATASIM_FIRMWARE_MAIN
#define fputc LATASIM_FIRMWARE_FPUTC

#endif /* LATASIM_FIRMWARE_SHIM_H */
