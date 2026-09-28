#pragma once
// A deterministic behavioural model of the Keil RTX 4 kernel behind the CMSIS-RTOS
// v1 API (docs/phase5/rtos.md).
//
// Scheduling follows RTX 4.82's own list operations (rt_List.c, rt_Task.c,
// rt_Robin.c, rt_System.c, rt_Mutex.c, rt_Event.c, rt_CMSIS.c in the CMSIS pack):
// priority-ordered ready list, preempted threads resuming first at their level,
// round-robin on the system tick, the delay list, signal flags, mutexes with
// priority inheritance, and virtual timers served by the timer thread. What firmware
// can observe (which thread runs when, return values, wake order) is meant to
// match; the kernel's memory layout, stacks and SVC mechanics are not modelled.
//
// Firmware threads run on fibers (fiber.hpp), one at a time, switched only by this
// kernel. Host code takes no virtual time; a thread uses processor time only by
// calling consume() (modelled workload) or through the configured cost of each
// kernel call. Time passes in run_until(), which advances the platform to its next
// event (a SysTick, a peripheral interrupt) or to the end of the running thread's
// workload, whichever is first, and switches threads where RTX would.
//
// The kernel knows nothing about any particular microcontroller: it reaches time,
// the tick interrupt and the trace through Platform.
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "trace/trace.hpp"

namespace latasim::rtos {

class Fiber;

// The machine the kernel runs on.
class Platform {
public:
    virtual ~Platform() = default;
    virtual std::uint64_t now() const = 0;  // core clock cycles
    // Cycles to the machine's next scheduled event, 0 if none.
    virtual std::uint64_t cycles_to_next_event() const = 0;
    // Advance time, taking every interrupt that becomes due on the way.
    virtual void advance(std::uint64_t cycles) = 0;
    // Program the system tick to interrupt every `period` cycles and call `tick`.
    virtual void start_tick(std::uint32_t period, std::function<void()> tick) = 0;
    // Called when the processor returns to thread mode after taking interrupts
    // (where RTX's PendSV handler runs).
    virtual void on_interrupts_done(std::function<void()> hook) = 0;
    virtual bool in_interrupt() const = 0;
    virtual void record(TraceEvent event) = 0;
};

// CMSIS-RTOS v1 status and event codes (cmsis_os.h), so results carry the same
// numbers firmware sees.
enum class Status : std::uint32_t {
    Ok = 0x00,
    EventSignal = 0x08,
    EventMessage = 0x10,
    EventTimeout = 0x40,
    ErrorParameter = 0x80,
    ErrorResource = 0x81,
    ErrorTimeoutResource = 0xC1,
    ErrorIsr = 0x82,
    ErrorPriority = 0x84,
    ErrorValue = 0x86,
    ErrorOs = 0xFF,
};

struct Event {
    Status status = Status::Ok;
    std::int32_t signals = 0;
};

// osPriority values: osPriorityIdle = -3 ... osPriorityRealtime = +3.
inline constexpr int kPriorityIdle = -3;
inline constexpr int kPriorityNormal = 0;
inline constexpr int kPriorityHigh = 2;
inline constexpr int kPriorityRealtime = 3;
inline constexpr int kPriorityError = 0x84;
inline constexpr std::uint32_t kWaitForever = 0xFFFFFFFFu;

// RTX_Conf_CM.c settings that change behaviour.
struct Config {
    std::uint32_t tick_period = 1'000'000;  // SysTick period in core cycles: os_trv + 1
    std::uint32_t tick_us = 10'000;         // OS_TICK: what millisecond conversions assume
    bool round_robin = true;                // OS_ROBIN
    std::uint32_t round_robin_ticks = 5;    // OS_ROBINTOUT
    int timer_priority = kPriorityHigh;     // OS_TIMERPRIO - 3; the timer thread's osPriority
    std::uint32_t timer_queue = 4;          // OS_TIMERCBQS
    // Modelled processor cycles each kernel call from a thread takes. RTX's SVC
    // path costs time; with 0, threads that only call the kernel (a yield loop)
    // would run forever at one instant.
    std::uint32_t call_cycles = 0;
    std::size_t stack_bytes = 256 * 1024;  // host stack per thread fiber
    std::uint64_t max_zero_time_switches = 1'000'000;  // then fail: no virtual time is passing
};

// Something the kernel cannot do or the firmware did wrong (not a CMSIS error
// return): an unsupported call, a thread that runs without time passing, ...
class RtosError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

enum class ThreadState { Ready, Running, Delayed, WaitingSignal, WaitingMutex, WaitingMessage, Terminated };
const char* to_string(ThreadState state);

using ThreadId = unsigned;  // 1, 2, ... in creation order; 0 = the idle demon
using MutexId = unsigned;   // 1, 2, ...
using TimerId = unsigned;   // 1, 2, ...

// Read-only views for tests and the desktop.
struct ThreadInfo {
    ThreadId id = 0;
    std::string name;
    ThreadState state = ThreadState::Ready;
    int base_level = 0;  // RTX level: 0 idle demon, 1-7 = osPriorityIdle-Realtime
    int level = 0;       // effective, including inheritance
    std::uint16_t signals = 0;
    std::uint16_t waiting_for = 0;
    std::optional<std::uint64_t> wake_tick;  // for delays and timed waits
    MutexId waiting_mutex = 0;
    std::uint64_t run_cycles = 0;  // virtual processor time spent running
};

struct MutexInfo {
    MutexId id = 0;
    ThreadId owner = 0;  // 0 = free
    unsigned level = 0;  // recursive lock count
    std::vector<ThreadId> waiters;
};

struct TimerInfo {
    TimerId id = 0;
    bool periodic = false;
    bool running = false;
    std::uint32_t period_ticks = 0;
};

// One stretch of virtual time during which one thread (or the idle demon) ran.
struct Interval {
    std::uint64_t start = 0;
    std::uint64_t end = 0;
    ThreadId thread = 0;
    bool operator==(const Interval&) const = default;
};

class Kernel {
public:
    Kernel(Platform& platform, Config config = {});
    ~Kernel();
    Kernel(const Kernel&) = delete;
    Kernel& operator=(const Kernel&) = delete;

    const Config& config() const { return config_; }

    // RTX's start-up (RTX_CM_lib.h): initialize the kernel, create the main thread
    // at osPriorityNormal running `main`, start the kernel. main then runs as a
    // thread once run_until gives it time.
    void start_main(std::function<void()> main);

    // ---- CMSIS-RTOS v1 operations (priorities are osPriority values) ----
    Status initialize();
    Status start();
    bool running() const { return running_; }
    std::uint64_t tick_count() const { return os_time_; }
    std::uint64_t now() const { return platform_.now(); }

    // Returns 0 on failure (bad priority).
    ThreadId create_thread(std::function<void()> entry, int priority, std::string name = {});
    ThreadId current_thread() const;  // 0 outside a thread
    Status terminate(ThreadId id);
    Status yield();
    Status set_priority(ThreadId id, int priority);
    int priority(ThreadId id) const;  // osPriority, kPriorityError if none
    Status delay(std::uint32_t millisec);

    std::int32_t signal_set(ThreadId id, std::int32_t signals);  // previous flags, 0x80000000 on error
    std::int32_t signal_clear(ThreadId id, std::int32_t signals);
    Event signal_wait(std::int32_t signals, std::uint32_t millisec);

    MutexId create_mutex();
    Status mutex_wait(MutexId id, std::uint32_t millisec);
    Status mutex_release(MutexId id);
    Status mutex_delete(MutexId id);

    TimerId create_timer(std::function<void()> callback, bool periodic);  // 0 if timers are off
    Status timer_start(TimerId id, std::uint32_t millisec);
    Status timer_stop(TimerId id);
    Status timer_delete(TimerId id);

    // The running thread uses `cycles` of processor time (modelled computation). It
    // can be preempted part way; it returns when all of it has been used.
    void consume(std::uint64_t cycles);

    // A point where the running thread may be preempted: after a store that took
    // an interrupt which readied a higher-priority thread.
    void preemption_point();

    // True while code runs on one of this kernel's thread fibers.
    bool in_thread() const { return on_thread(); }
    // From a thread: stop it for good because of `error` (a firmware fault, an
    // unsupported call). Its fiber is abandoned, not unwound, so no exception crosses
    // C frames; run_until rethrows `error`. The kernel cannot run further.
    [[noreturn]] void abandon_thread(std::exception_ptr error);

    // ---- execution ----
    void run_until(std::uint64_t cycle);
    void run_for(std::uint64_t cycles) { run_until(platform_.now() + cycles); }

    // ---- inspection ----
    std::vector<ThreadInfo> threads() const;  // idle demon first, then by id
    std::optional<ThreadInfo> thread(ThreadId id) const;
    ThreadId running_thread() const;  // 0 = idle demon
    std::vector<MutexInfo> mutexes() const;
    std::vector<TimerInfo> timers() const;
    const std::vector<Interval>& timeline() const { return timeline_; }  // closed up to now
    std::uint64_t idle_cycles() const;
    std::uint64_t call_cycles_total() const { return call_cycles_total_; }
    void set_thread_name(ThreadId id, std::string name);

private:
    struct Tcb;
    struct Mutex;
    struct Timer;

    Tcb& tcb(ThreadId id);
    const Tcb& tcb(ThreadId id) const;
    Tcb* find(ThreadId id);
    const Tcb* find(ThreadId id) const;
    bool on_thread() const;
    void require_thread(const char* call) const;
    void enter_call(const char* call);
    void after_call();

    // RTX list and task primitives.
    void put_prio(std::deque<ThreadId>& list, ThreadId id);
    ThreadId get_first(std::deque<ThreadId>& list);
    void put_rdy_first(ThreadId id);
    static void remove(std::deque<ThreadId>& list, ThreadId id);
    int rdy_prio() const;
    void switch_req(ThreadId id);
    void dispatch(ThreadId next);  // 0 = none: run the highest ready
    void block(std::uint32_t ticks, ThreadState state);
    void put_dly(ThreadId id, std::uint32_t ticks);
    void rmv_dly(ThreadId id);
    void dec_dly();
    void resort(ThreadId id);
    void task_prio(ThreadId id, int level);
    void delete_task(ThreadId id);
    void release_owned(Tcb& t);
    void remove_mutex_from_owner(Tcb& t, MutexId id);
    void restore_priority(Tcb& t, MutexId released);
    void evt_set(ThreadId id, std::uint16_t flags);
    std::uint16_t ms2tick(std::uint32_t millisec) const;

    void tick();
    void post_service();
    void timer_insert(TimerId id, std::uint32_t ticks);
    bool timer_remove(TimerId id);
    void timer_tick();
    void timer_thread_body();

    void record(RtosOp op, ThreadId thread, int level, std::uint16_t object = 0, std::uint32_t value = 0);
    void account();
    void perform_switch();
    void run_thread_slice(std::uint64_t until);
    void start_fiber(Tcb& t);

    Platform& platform_;
    Config config_;
    std::vector<std::unique_ptr<Tcb>> threads_;  // [0] = idle demon
    std::vector<std::unique_ptr<Mutex>> mutexes_;
    std::vector<std::unique_ptr<Timer>> timers_;
    std::deque<ThreadId> ready_;
    std::deque<ThreadId> delayed_;  // in wake order
    std::optional<ThreadId> run_;   // os_tsk.run: nullopt after a thread terminated itself
    ThreadId next_ = 0;             // os_tsk.next
    ThreadId robin_task_ = 0;
    bool robin_valid_ = false;
    std::uint16_t robin_time_ = 0;
    std::uint64_t os_time_ = 0;
    bool initialized_ = false;
    bool running_ = false;
    bool tick_started_ = false;
    ThreadId timer_thread_ = 0;
    std::deque<TimerId> timer_active_;       // os_timer_head, in expiry order
    std::deque<TimerId> timer_messages_;     // osTimerMessageQ
    std::deque<TimerId> pending_messages_;   // sent from the tick, delivered after it (os_psq)
    std::deque<std::pair<ThreadId, std::uint16_t>> isr_signals_;  // os_psq
    bool switch_pending_ = false;
    std::vector<Interval> timeline_;
    std::uint64_t accounted_to_ = 0;
    std::uint64_t call_cycles_total_ = 0;
    std::uint64_t zero_time_switches_ = 0;
    std::uint64_t last_switch_time_ = 0;
    ThreadId executing_ = 0;  // whose fiber is on the host stack (0: none)
    std::exception_ptr fault_;
};

}  // namespace latasim::rtos
