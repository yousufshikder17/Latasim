// CMSIS-RTOS v1 for host-compiled firmware, over the bound rtos::Kernel
// (docs/phase5/rtos.md). Compiled against RTX 4's own cmsis_os.h from the CMSIS
// pack, so firmware sees the real types, macros and constants.
//
// Supported: kernel control, threads, osDelay, signals, mutexes, timers. Semaphores,
// memory pools, message and mail queues, os_suspend/os_resume are not modelled:
// calling one stops the calling thread with an error (see host/rtos_binding.hpp)
// rather than returning a value firmware might misread.
//
// Object IDs point at small per-binding records holding the kernel's stable numbers;
// traces and the desktop show those numbers, never pointers.
#include "cmsis_os.h"

#include <map>
#include <memory>
#include <stdexcept>
#include <string>

#include "host/rtos_binding.hpp"

struct os_thread_cb {
    latasim::rtos::ThreadId id;
};
struct os_timer_cb {
    latasim::rtos::TimerId id;
};
struct os_mutex_cb {
    latasim::rtos::MutexId id;
};

namespace {

using latasim::rtos::Kernel;
using latasim::rtos::Status;

// Handles for the kernel currently bound; rebuilt whenever a different binding is made.
struct Handles {
    std::map<latasim::rtos::ThreadId, std::unique_ptr<os_thread_cb>> threads;
    std::map<const osTimerDef_t*, std::unique_ptr<os_timer_cb>> timers;
    std::map<const osMutexDef_t*, std::unique_ptr<os_mutex_cb>> mutexes;
};

Handles& handles() {
    auto& slot = latasim::host::bound_rtos_state();
    if (!slot) slot = std::make_shared<Handles>();
    return *std::static_pointer_cast<Handles>(slot);
}

Kernel& kernel(const char* caller) { return latasim::host::require_bound_kernel(caller); }

osThreadId thread_handle(latasim::rtos::ThreadId id) {
    if (id == 0) return nullptr;
    auto& p = handles().threads[id];
    if (!p) p = std::make_unique<os_thread_cb>(os_thread_cb{id});
    return p.get();
}

latasim::rtos::ThreadId id_of(osThreadId t) { return t == nullptr ? 0 : t->id; }

osStatus status(Status s) { return static_cast<osStatus>(s); }

// Kernel errors (RtosError, a fault in a timer callback ...) stop the calling thread.
template <typename F>
auto guarded(const char* caller, F f) -> decltype(f()) {
    try {
        return f();
    } catch (const std::exception& e) {
        latasim::host::rtos_fault(caller, e.what());
    }
}

[[noreturn]] void unsupported(const char* call) {
    latasim::host::rtos_fault(call, "not supported by Latasim's RTOS model (docs/phase5/rtos.md)");
}

}  // namespace

extern "C" {

// RTX_CM_lib.h's constants for OS_CLOCK = 100 MHz.
uint32_t const os_tickfreq = 100'000'000u;
uint16_t const os_tickus_i = 100u;
uint16_t const os_tickus_f = 0u;

osStatus osKernelInitialize(void) {
    return guarded("osKernelInitialize", [] { return status(kernel("osKernelInitialize").initialize()); });
}

osStatus osKernelStart(void) {
    return guarded("osKernelStart", [] { return status(kernel("osKernelStart").start()); });
}

int32_t osKernelRunning(void) { return kernel("osKernelRunning").running() ? 1 : 0; }

uint32_t osKernelSysTick(void) { return static_cast<uint32_t>(kernel("osKernelSysTick").now()); }

osThreadId osThreadCreate(const osThreadDef_t* thread_def, void* argument) {
    return guarded("osThreadCreate", [&]() -> osThreadId {
        if (thread_def == nullptr || thread_def->pthread == nullptr) return nullptr;
        const os_pthread entry = thread_def->pthread;
        const auto id = kernel("osThreadCreate").create_thread([entry, argument] { entry(argument); },
                                                               static_cast<int>(thread_def->tpriority));
        return thread_handle(id);
    });
}

osThreadId osThreadGetId(void) { return thread_handle(kernel("osThreadGetId").current_thread()); }

osStatus osThreadTerminate(osThreadId thread_id) {
    return guarded("osThreadTerminate", [&] { return status(kernel("osThreadTerminate").terminate(id_of(thread_id))); });
}

osStatus osThreadYield(void) {
    return guarded("osThreadYield", [] { return status(kernel("osThreadYield").yield()); });
}

osStatus osThreadSetPriority(osThreadId thread_id, osPriority priority) {
    return guarded("osThreadSetPriority", [&] {
        return status(kernel("osThreadSetPriority").set_priority(id_of(thread_id), static_cast<int>(priority)));
    });
}

osPriority osThreadGetPriority(osThreadId thread_id) {
    return static_cast<osPriority>(kernel("osThreadGetPriority").priority(id_of(thread_id)));
}

osStatus osDelay(uint32_t millisec) {
    return guarded("osDelay", [&] { return status(kernel("osDelay").delay(millisec)); });
}

osTimerId osTimerCreate(const osTimerDef_t* timer_def, os_timer_type type, void* argument) {
    return guarded("osTimerCreate", [&]() -> osTimerId {
        if (timer_def == nullptr || timer_def->ptimer == nullptr) return nullptr;
        if (type != osTimerOnce && type != osTimerPeriodic) return nullptr;
        auto& slot = handles().timers[timer_def];
        if (slot) return nullptr;  // RTX: the control block is already in use
        const os_ptimer fn = timer_def->ptimer;
        const auto id = kernel("osTimerCreate").create_timer([fn, argument] { fn(argument); }, type == osTimerPeriodic);
        if (id == 0) return nullptr;
        slot = std::make_unique<os_timer_cb>(os_timer_cb{id});
        return slot.get();
    });
}

osStatus osTimerStart(osTimerId timer_id, uint32_t millisec) {
    if (timer_id == nullptr) return osErrorParameter;
    return guarded("osTimerStart", [&] { return status(kernel("osTimerStart").timer_start(timer_id->id, millisec)); });
}

osStatus osTimerStop(osTimerId timer_id) {
    if (timer_id == nullptr) return osErrorParameter;
    return guarded("osTimerStop", [&] { return status(kernel("osTimerStop").timer_stop(timer_id->id)); });
}

osStatus osTimerDelete(osTimerId timer_id) {
    if (timer_id == nullptr) return osErrorParameter;
    return guarded("osTimerDelete", [&] {
        const Status s = kernel("osTimerDelete").timer_delete(timer_id->id);
        if (s == Status::Ok)
            for (auto it = handles().timers.begin(); it != handles().timers.end(); ++it)
                if (it->second.get() == timer_id) {
                    handles().timers.erase(it);  // the definition can be created again
                    break;
                }
        return status(s);
    });
}

int32_t osSignalSet(osThreadId thread_id, int32_t signals) {
    return guarded("osSignalSet", [&] { return kernel("osSignalSet").signal_set(id_of(thread_id), signals); });
}

int32_t osSignalClear(osThreadId thread_id, int32_t signals) {
    return guarded("osSignalClear", [&] { return kernel("osSignalClear").signal_clear(id_of(thread_id), signals); });
}

osEvent osSignalWait(int32_t signals, uint32_t millisec) {
    return guarded("osSignalWait", [&] {
        const auto e = kernel("osSignalWait").signal_wait(signals, millisec);
        osEvent out{};
        out.status = static_cast<osStatus>(e.status);
        out.value.signals = e.signals;
        return out;
    });
}

osMutexId osMutexCreate(const osMutexDef_t* mutex_def) {
    return guarded("osMutexCreate", [&]() -> osMutexId {
        if (mutex_def == nullptr) return nullptr;
        auto& slot = handles().mutexes[mutex_def];
        if (slot) return nullptr;  // RTX: already initialised
        slot = std::make_unique<os_mutex_cb>(os_mutex_cb{kernel("osMutexCreate").create_mutex()});
        return slot.get();
    });
}

osStatus osMutexWait(osMutexId mutex_id, uint32_t millisec) {
    if (mutex_id == nullptr) return osErrorParameter;
    return guarded("osMutexWait", [&] { return status(kernel("osMutexWait").mutex_wait(mutex_id->id, millisec)); });
}

osStatus osMutexRelease(osMutexId mutex_id) {
    if (mutex_id == nullptr) return osErrorParameter;
    return guarded("osMutexRelease", [&] { return status(kernel("osMutexRelease").mutex_release(mutex_id->id)); });
}

osStatus osMutexDelete(osMutexId mutex_id) {
    if (mutex_id == nullptr) return osErrorParameter;
    return guarded("osMutexDelete", [&] { return status(kernel("osMutexDelete").mutex_delete(mutex_id->id)); });
}

// ---- not modelled ----

osSemaphoreId osSemaphoreCreate(const osSemaphoreDef_t*, int32_t) { unsupported("osSemaphoreCreate"); }
int32_t osSemaphoreWait(osSemaphoreId, uint32_t) { unsupported("osSemaphoreWait"); }
osStatus osSemaphoreRelease(osSemaphoreId) { unsupported("osSemaphoreRelease"); }
osStatus osSemaphoreDelete(osSemaphoreId) { unsupported("osSemaphoreDelete"); }
osPoolId osPoolCreate(const osPoolDef_t*) { unsupported("osPoolCreate"); }
void* osPoolAlloc(osPoolId) { unsupported("osPoolAlloc"); }
void* osPoolCAlloc(osPoolId) { unsupported("osPoolCAlloc"); }
osStatus osPoolFree(osPoolId, void*) { unsupported("osPoolFree"); }
osMessageQId osMessageCreate(const osMessageQDef_t*, osThreadId) { unsupported("osMessageCreate"); }
osStatus osMessagePut(osMessageQId, uint32_t, uint32_t) { unsupported("osMessagePut"); }
osEvent osMessageGet(osMessageQId, uint32_t) { unsupported("osMessageGet"); }
osMailQId osMailCreate(const osMailQDef_t*, osThreadId) { unsupported("osMailCreate"); }
void* osMailAlloc(osMailQId, uint32_t) { unsupported("osMailAlloc"); }
void* osMailCAlloc(osMailQId, uint32_t) { unsupported("osMailCAlloc"); }
osStatus osMailPut(osMailQId, void*) { unsupported("osMailPut"); }
osEvent osMailGet(osMailQId, uint32_t) { unsupported("osMailGet"); }
osStatus osMailFree(osMailQId, void*) { unsupported("osMailFree"); }
uint32_t os_suspend(void) { unsupported("os_suspend"); }
void os_resume(uint32_t) { unsupported("os_resume"); }

}  // extern "C"
