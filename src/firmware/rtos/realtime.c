/* Real-time scheduling: rate-monotonic periodic tasks and priority inversion
 * (workloads.h). */
#include <string.h>

#include "cmsis_os.h"
#include "latasim_rtos.h"
#include "workloads.h"

/* ---- rate-monotonic ---- */

volatile uint32_t rms_release[RMS_TASKS][RMS_JOBS];
volatile uint32_t rms_complete[RMS_TASKS][RMS_JOBS];
volatile uint32_t rms_jobs[RMS_TASKS];
static uint32_t rms_released[RMS_TASKS];
static osThreadId rms_id[RMS_TASKS];
/* Index 0 = A, 1 = B, 2 = C. */
static const uint32_t rms_work_ms[RMS_TASKS] = {150, 100, 50};

static void rms_release_job(uint32_t n) {
    if (rms_released[n] < RMS_JOBS) rms_release[n][rms_released[n]] = osKernelSysTick();
    rms_released[n]++;
    osSignalSet(rms_id[n], 0x01);
}

static void rms_task(void const *argument) {
    const uint32_t n = (uint32_t)(uintptr_t)argument;
    for (;;) {
        osSignalWait(0x01, osWaitForever);
        latasim_consume_us(rms_work_ms[n] * 1000u);
        if (rms_jobs[n] < RMS_JOBS) rms_complete[n][rms_jobs[n]] = osKernelSysTick();
        rms_jobs[n]++;
    }
}
osThreadDef(rms_task, osPriorityNormal, RMS_TASKS, 0);

static void rms_period_200(void const *argument) {
    (void)argument;
    rms_release_job(2);
}
static void rms_period_400(void const *argument) {
    (void)argument;
    rms_release_job(1);
    rms_release_job(0);
}
osTimerDef(rms_timer_200, rms_period_200);
osTimerDef(rms_timer_400, rms_period_400);

void rms_main(void) {
    static const osPriority priority[RMS_TASKS] = {osPriorityNormal, osPriorityAboveNormal, osPriorityHigh};
    uint32_t n;
    osKernelInitialize();
    for (n = 0; n < RMS_TASKS; ++n) {
        rms_id[n] = osThreadCreate(osThread(rms_task), (void *)(uintptr_t)n);
        osThreadSetPriority(rms_id[n], priority[n]);
    }
    osTimerStart(osTimerCreate(osTimer(rms_timer_200), osTimerPeriodic, NULL), 200);
    osTimerStart(osTimerCreate(osTimer(rms_timer_400), osTimerPeriodic, NULL), 400);
    for (n = 0; n < RMS_TASKS; ++n) rms_release_job(n); /* all released at t = 0 */
    osKernelStart();
}

void rms_reset(void) {
    memset((void *)rms_release, 0, sizeof rms_release);
    memset((void *)rms_complete, 0, sizeof rms_complete);
    memset((void *)rms_jobs, 0, sizeof rms_jobs);
    memset(rms_released, 0, sizeof rms_released);
}

/* ---- priority inversion ---- */

volatile uint32_t inv_high_got_resource_at;
volatile uint32_t inv_medium_done_at;
volatile uint32_t inv_low_done_at;
static int inv_mode;
static volatile int inv_busy;
static osThreadId inv_low_id;
static osMutexId inv_mutex;
osMutexDef(inv_mutex);

static void inv_low(void const *argument) {
    (void)argument;
    if (inv_mode == 2) osMutexWait(inv_mutex, osWaitForever);
    else inv_busy = 1;
    latasim_consume_us(80000); /* the critical section's work */
    if (inv_mode == 2) {
        osMutexRelease(inv_mutex);
    } else {
        inv_busy = 0;
        if (inv_mode == 1) osThreadSetPriority(osThreadGetId(), osPriorityLow); /* undo the elevation */
    }
    inv_low_done_at = osKernelSysTick();
}

static void inv_medium(void const *argument) {
    (void)argument;
    osDelay(50);
    latasim_consume_us(100000);
    inv_medium_done_at = osKernelSysTick();
}

static void inv_high(void const *argument) {
    (void)argument;
    osDelay(60);
    if (inv_mode == 2) {
        osMutexWait(inv_mutex, osWaitForever);
        inv_high_got_resource_at = osKernelSysTick();
        osMutexRelease(inv_mutex);
        return;
    }
    if (inv_mode == 1 && inv_busy) osThreadSetPriority(inv_low_id, osPriorityHigh); /* elevate the holder */
    while (inv_busy) osDelay(10);
    inv_high_got_resource_at = osKernelSysTick();
}

osThreadDef(inv_low, osPriorityLow, 1, 0);
osThreadDef(inv_medium, osPriorityNormal, 1, 0);
osThreadDef(inv_high, osPriorityHigh, 1, 0);

void inv_main(void) {
    osKernelInitialize();
    inv_mutex = osMutexCreate(osMutex(inv_mutex));
    inv_low_id = osThreadCreate(osThread(inv_low), NULL);
    osThreadCreate(osThread(inv_medium), NULL);
    osThreadCreate(osThread(inv_high), NULL);
    osKernelStart();
}

void inv_reset(int mode) {
    inv_mode = mode;
    inv_busy = 0;
    inv_high_got_resource_at = inv_medium_done_at = inv_low_done_at = 0;
}
