/* Latasim extensions for host-compiled RTOS firmware (docs/phase5/rtos.md).
 *
 * Host-compiled code takes no virtual time. Firmware that models computation (a
 * scheduling exercise's busy work, an RMS task's execution time) calls these
 * instead of spinning: the calling thread uses that much processor time, and can be
 * preempted part way, exactly as a busy loop of that length would be on the chip.
 * A plain busy loop never lets virtual time pass on the host.
 */
#ifndef LATASIM_RTOS_H
#define LATASIM_RTOS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Core clock cycles (100 MHz). */
void latasim_consume_cycles(uint32_t cycles);
/* Microseconds at the 100 MHz core clock. */
void latasim_consume_us(uint32_t microseconds);

#ifdef __cplusplus
}
#endif

#endif /* LATASIM_RTOS_H */
