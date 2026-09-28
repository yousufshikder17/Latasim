/* Scheduling basics: round-robin, preemption, yield, delay (workloads.h). */
#include <string.h>

#include "cmsis_os.h"
#include "latasim_rtos.h"
#include "workloads.h"

/* ---- round-robin ---- */

volatile uint32_t rr_steps[RR_THREADS];
volatile uint32_t rr_done_at[RR_THREADS];
static const uint32_t rr_work_ms[RR_THREADS] = {40, 30, 20};

static void rr_worker(void const *argument) {
    const uint32_t n = (uint32_t)(uintptr_t)argument;
    uint32_t i;
    for (i = 0; i < rr_work_ms[n]; ++i) {
        latasim_consume_us(1000);
        rr_steps[n]++;
    }
    rr_done_at[n] = osKernelSysTick();
}
osThreadDef(rr_worker, osPriorityNormal, RR_THREADS, 0);

void rr_main(void) {
    uintptr_t n;
    osKernelInitialize();
    for (n = 0; n < RR_THREADS; ++n) osThreadCreate(osThread(rr_worker), (void *)n);
    osKernelStart();
}

void rr_reset(void) {
    memset((void *)rr_steps, 0, sizeof rr_steps);
    memset((void *)rr_done_at, 0, sizeof rr_done_at);
}

/* ---- preemptive ---- */

volatile double pre_result[PRE_TASKS];
volatile uint32_t pre_finish_order[PRE_TASKS];
volatile uint32_t pre_finished;

static const osPriority pre_priority[PRE_TASKS] = {osPriorityAboveNormal, osPriorityNormal, osPriorityHigh,
                                                   osPriorityAboveNormal, osPriorityNormal};
static const uint32_t pre_work_ms[PRE_TASKS] = {20, 30, 10, 15, 25};

static double pre_compute(uint32_t n) {
    double sum = 0.0;
    uint32_t k;
    switch (n) {
    case 0: for (k = 0; k <= 256; ++k) sum += (double)(k + (k + 2)); break;       /* sum of pairs */
    case 1: for (k = 1; k <= 16; ++k) sum += 1.0 / (double)(k * k); break;          /* partial zeta(2) */
    case 2: for (k = 1; k <= 16; ++k) sum += (double)(k + 1) / (double)k; break;    /* sum (k+1)/k */
    case 3: { double term = 1.0; sum = 1.0; for (k = 1; k <= 5; ++k) { term *= 5.0 / k; sum += term; } } break;
    default: for (k = 1; k <= 12; ++k) sum += 3.14159265358979 * (double)k * 4.0; break; /* k*pi*r^2, r = 2 */
    }
    return sum;
}

static void pre_task(void const *argument) {
    const uint32_t n = (uint32_t)(uintptr_t)argument;
    pre_result[n] = pre_compute(n);
    latasim_consume_us(pre_work_ms[n] * 1000u);
    pre_finish_order[pre_finished++] = n;
}
osThreadDef(pre_task, osPriorityNormal, PRE_TASKS, 0);

void pre_main(void) {
    uint32_t n;
    osKernelInitialize();
    for (n = 0; n < PRE_TASKS; ++n) {
        const osThreadId id = osThreadCreate(osThread(pre_task), (void *)(uintptr_t)n);
        osThreadSetPriority(id, pre_priority[n]);
    }
    osKernelStart();
}

void pre_reset(void) {
    memset((void *)pre_result, 0, sizeof pre_result);
    memset((void *)pre_finish_order, 0, sizeof pre_finish_order);
    pre_finished = 0;
}

/* ---- yield ---- */

volatile uint32_t yield_count[2];
volatile char yield_order[64];
static uint32_t yield_pos;

static void yielder(void const *argument) {
    const uint32_t n = (uint32_t)(uintptr_t)argument;
    for (;;) {
        yield_count[n]++;
        if (yield_pos < sizeof yield_order - 1) yield_order[yield_pos++] = (char)('A' + n);
        osThreadYield();
    }
}
osThreadDef(yielder, osPriorityNormal, 2, 0);

void yield_main(void) {
    osKernelInitialize();
    osThreadCreate(osThread(yielder), (void *)0);
    osThreadCreate(osThread(yielder), (void *)1);
    osKernelStart();
}

void yield_reset(void) {
    memset((void *)yield_count, 0, sizeof yield_count);
    memset((void *)yield_order, 0, sizeof yield_order);
    yield_pos = 0;
}

/* ---- delay ---- */

volatile uint32_t delay_count[2];

static void delayer(void const *argument) {
    const uint32_t n = (uint32_t)(uintptr_t)argument;
    for (;;) {
        delay_count[n]++;
        osDelay(n == 0 ? 10 : 20);
    }
}
osThreadDef(delayer, osPriorityNormal, 2, 0);

void delay_main(void) {
    osKernelInitialize();
    osThreadCreate(osThread(delayer), (void *)0);
    osThreadCreate(osThread(delayer), (void *)1);
    osKernelStart();
}

void delay_reset(void) { memset((void *)delay_count, 0, sizeof delay_count); }
