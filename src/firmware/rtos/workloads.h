/* Representative CMSIS-RTOS v1 firmware for Latasim's RTOS model
 * (docs/phase5/rtos.md). Each workload is C written against RTX 4's cmsis_os.h
 * and runs unchanged on the host: `<name>_main` is its main() (run as RTX's main
 * thread), `<name>_reset` puts its statics back for another run in the same process.
 * Computation is modelled with latasim_consume_us (latasim_rtos.h).
 *
 * Timestamps come from osKernelSysTick(), which counts core cycles (100 MHz).
 */
#ifndef LATASIM_FIRMWARE_RTOS_WORKLOADS_H
#define LATASIM_FIRMWARE_RTOS_WORKLOADS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Round-robin: three equal-priority threads, each a finite amount of work done in
 * 1 ms steps, counting its steps; the configured time slice interleaves them. */
#define RR_THREADS 3
extern volatile uint32_t rr_steps[RR_THREADS];
extern volatile uint32_t rr_done_at[RR_THREADS]; /* osKernelSysTick when finished */
void rr_main(void);
void rr_reset(void);

/* Preemptive: five finite computations at three priorities; each records its result
 * and the order it finished in. */
#define PRE_TASKS 5
extern volatile double pre_result[PRE_TASKS];
extern volatile uint32_t pre_finish_order[PRE_TASKS]; /* task indices, in finishing order */
extern volatile uint32_t pre_finished;
void pre_main(void);
void pre_reset(void);

/* Yield: two equal threads that count and hand over with osThreadYield. */
extern volatile uint32_t yield_count[2];
extern volatile char yield_order[64];
void yield_main(void);
void yield_reset(void);

/* Delay: two threads counting with osDelay(10) and osDelay(20). */
extern volatile uint32_t delay_count[2];
void delay_main(void);
void delay_reset(void);

/* Services: five cooperating threads with signals, a mutex-protected log and finite
 * lifetimes (memory, cpu, application, device, user). */
extern volatile uint32_t svc_counter[5];
extern char svc_log[64];
extern volatile char svc_order[16]; /* first letter of each thread as it finishes */
void svc_main(void);
void svc_reset(void);

/* Virtual timers: three periodic timers (50, 80, 120 ms) whose callbacks signal a
 * thread that toggles LEDs 0-2. */
extern volatile uint32_t vt_fired[3];
void vt_main(void);
void vt_reset(void);

/* Rate-monotonic: three periodic tasks released by virtual timers. Periods and
 * computation times in ms: C (200, 50), B (400, 100), A (400, 200); priorities by
 * rate. Records each job's release and completion times. */
#define RMS_TASKS 3
#define RMS_JOBS 8
extern volatile uint32_t rms_release[RMS_TASKS][RMS_JOBS];
extern volatile uint32_t rms_complete[RMS_TASKS][RMS_JOBS];
extern volatile uint32_t rms_jobs[RMS_TASKS];
void rms_main(void);
void rms_reset(void);

/* Priority inversion: Low holds a resource for 80 ms of work from t = 0, Medium
 * wakes at 50 ms and computes 100 ms, High wakes at 60 ms and needs the resource.
 * mode 0: the resource is a flag (raw inversion);
 * mode 1: the same flag, but High raises Low's priority while it waits;
 * mode 2: the resource is an RTX mutex (priority inheritance). */
extern volatile uint32_t inv_high_got_resource_at; /* osKernelSysTick */
extern volatile uint32_t inv_medium_done_at;
extern volatile uint32_t inv_low_done_at;
void inv_main(void);
void inv_reset(int mode);

#ifdef __cplusplus
}
#endif

#endif /* LATASIM_FIRMWARE_RTOS_WORKLOADS_H */
