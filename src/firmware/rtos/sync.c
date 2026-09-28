/* Synchronisation: signals and a mutex between cooperating threads, and virtual
 * timers driving LEDs (workloads.h). */
#include <string.h>

#include "Board_LED.h"
#include "cmsis_os.h"
#include "latasim_rtos.h"
#include "workloads.h"

/* ---- services ---- */

volatile uint32_t svc_counter[5];
char svc_log[64];
volatile char svc_order[16];
static uint32_t svc_done;
static osThreadId svc_memory_id, svc_cpu_id, svc_app_id, svc_device_id;
static osMutexId svc_log_mutex;
osMutexDef(svc_log_mutex);

static void svc_finished(char letter) {
    if (svc_done < sizeof svc_order - 1) svc_order[svc_done++] = letter;
}

static void svc_memory(void const *argument) {
    volatile uint32_t word = 0;
    (void)argument;
    svc_counter[0]++;
    word |= 1u << 7; /* a single-bit update, as a bit-band store would make */
    latasim_consume_us(2000);
    osSignalSet(svc_cpu_id, 0x01);
    osSignalWait(0x02, osWaitForever);
    osDelay(10);
    svc_finished('M');
}

static void svc_cpu(void const *argument) {
    uint32_t x = 0x80000001u, shifted;
    (void)argument;
    osSignalWait(0x01, osWaitForever);
    svc_counter[1]++;
    shifted = (x & 1u) ? (x >> 3) | (x << 29) : x << 1; /* conditional, barrel shift */
    (void)shifted;
    latasim_consume_us(3000);
    osSignalSet(svc_memory_id, 0x02);
    svc_finished('C');
}

static void svc_log_append(const char *text) {
    size_t used, n;
    osMutexWait(svc_log_mutex, osWaitForever);
    used = strlen(svc_log);
    n = strlen(text);
    if (n > sizeof svc_log - 1 - used) n = sizeof svc_log - 1 - used;
    memcpy(svc_log + used, text, n);
    svc_log[used + n] = '\0';
    osMutexRelease(svc_log_mutex);
}

static void svc_app(void const *argument) {
    (void)argument;
    svc_log_append("app: start of message");
    osSignalSet(svc_device_id, 0x08); /* the device writes only after this */
    osSignalWait(0x04, osWaitForever);
    svc_counter[2]++;
    osDelay(10);
    svc_finished('A');
}

static void svc_device(void const *argument) {
    (void)argument;
    osSignalWait(0x08, osWaitForever);
    svc_log_append(", end");
    osSignalSet(svc_app_id, 0x04);
    svc_counter[3]++;
    osDelay(10);
    svc_finished('D');
}

static void svc_user(void const *argument) {
    (void)argument;
    svc_counter[4]++;
    osDelay(10);
    svc_finished('U');
}

osThreadDef(svc_memory, osPriorityAboveNormal, 1, 0);
osThreadDef(svc_cpu, osPriorityAboveNormal, 1, 0);
osThreadDef(svc_app, osPriorityNormal, 1, 0);
osThreadDef(svc_device, osPriorityNormal, 1, 0);
osThreadDef(svc_user, osPriorityBelowNormal, 1, 0);

void svc_main(void) {
    osKernelInitialize();
    svc_log_mutex = osMutexCreate(osMutex(svc_log_mutex));
    svc_memory_id = osThreadCreate(osThread(svc_memory), NULL);
    svc_cpu_id = osThreadCreate(osThread(svc_cpu), NULL);
    svc_app_id = osThreadCreate(osThread(svc_app), NULL);
    svc_device_id = osThreadCreate(osThread(svc_device), NULL);
    osThreadCreate(osThread(svc_user), NULL);
    osKernelStart();
}

void svc_reset(void) {
    memset((void *)svc_counter, 0, sizeof svc_counter);
    memset(svc_log, 0, sizeof svc_log);
    memset((void *)svc_order, 0, sizeof svc_order);
    svc_done = 0;
}

/* ---- virtual timers ---- */

volatile uint32_t vt_fired[3];
static osThreadId vt_led_id;

static void vt_callback(void const *argument) {
    const uint32_t n = (uint32_t)(uintptr_t)argument;
    vt_fired[n]++;
    osSignalSet(vt_led_id, (int32_t)(1u << n));
}

osTimerDef(vt_timer0, vt_callback);
osTimerDef(vt_timer1, vt_callback);
osTimerDef(vt_timer2, vt_callback);

static void vt_leds(void const *argument) {
    uint32_t state = 0, n;
    (void)argument;
    for (;;) {
        const osEvent e = osSignalWait(0, osWaitForever);
        for (n = 0; n < 3; ++n)
            if ((uint32_t)e.value.signals & (1u << n)) {
                state ^= 1u << n;
                if (state & (1u << n)) LED_On(n);
                else LED_Off(n);
            }
    }
}
osThreadDef(vt_leds, osPriorityNormal, 1, 0);

void vt_main(void) {
    osKernelInitialize();
    LED_Initialize();
    vt_led_id = osThreadCreate(osThread(vt_leds), NULL);
    osTimerStart(osTimerCreate(osTimer(vt_timer0), osTimerPeriodic, (void *)0), 50);
    osTimerStart(osTimerCreate(osTimer(vt_timer1), osTimerPeriodic, (void *)1), 80);
    osTimerStart(osTimerCreate(osTimer(vt_timer2), osTimerPeriodic, (void *)2), 120);
    osKernelStart();
}

void vt_reset(void) { memset((void *)vt_fired, 0, sizeof vt_fired); }
