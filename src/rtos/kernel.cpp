#include "rtos/kernel.hpp"

#include <algorithm>
#include <cstdlib>
#include <utility>

#include "rtos/fiber.hpp"

namespace latasim::rtos {
namespace {

constexpr ThreadId kNone = 0xFFFFFFFFu;  // RTX's NULL task pointer (0 is the idle demon)
constexpr int kLevelBoost = 255;          // os_tsk.run->prio during osKernelInitialize
constexpr std::uint16_t kNoTimeout = 0xFFFF;

int level_of(int priority) { return priority - kPriorityIdle + 1; }  // osPriority -> RTX level

}  // namespace

const char* to_string(ThreadState state) {
    switch (state) {
    case ThreadState::Ready: return "Ready";
    case ThreadState::Running: return "Running";
    case ThreadState::Delayed: return "Delayed";
    case ThreadState::WaitingSignal: return "WaitingSignal";
    case ThreadState::WaitingMutex: return "WaitingMutex";
    case ThreadState::WaitingMessage: return "WaitingMessage";
    case ThreadState::Terminated: return "Terminated";
    }
    return "?";
}

struct Kernel::Tcb {
    ThreadId id = 0;
    std::string name;
    std::function<void()> entry;
    int base = 0;  // prio_base
    int prio = 0;  // prio: effective, raised by inheritance
    ThreadState state = ThreadState::Ready;
    bool wait_and = false;  // WAIT_AND rather than WAIT_OR
    std::uint16_t events = 0;
    std::uint16_t waits = 0;
    bool delayed = false;  // in the delay list
    std::uint64_t wake_tick = 0;
    MutexId wait_mutex = 0;
    std::deque<MutexId> owned;  // p_mlnk: most recently acquired first
    Event ret;                  // what a blocked call returns when the thread resumes
    TimerId message = 0;        // the timer thread's received message
    std::unique_ptr<Fiber> fiber;
    std::uint64_t consume_left = 0;
    std::uint64_t run_cycles = 0;
};

struct Kernel::Mutex {
    MutexId id = 0;
    bool valid = true;
    unsigned level = 0;
    ThreadId owner = kNone;
    std::deque<ThreadId> waiters;  // priority order
};

struct Kernel::Timer {
    TimerId id = 0;
    std::function<void()> callback;
    bool periodic = false;
    enum class State { Invalid, Stopped, Running } state = State::Stopped;
    std::uint32_t tcnt = 0;  // delta to the previous active timer's expiry
    std::uint32_t icnt = 0;  // period
};

Kernel::Kernel(Platform& platform, Config config) : platform_(platform), config_(config) {
    platform_.on_interrupts_done([this] { post_service(); });
}

Kernel::~Kernel() = default;

// ---- helpers ----

Kernel::Tcb& Kernel::tcb(ThreadId id) { return *threads_.at(id); }
const Kernel::Tcb& Kernel::tcb(ThreadId id) const { return *threads_.at(id); }

Kernel::Tcb* Kernel::find(ThreadId id) {
    if (id == 0 || id >= threads_.size()) return nullptr;
    Tcb& t = *threads_[id];
    return t.state == ThreadState::Terminated ? nullptr : &t;
}
const Kernel::Tcb* Kernel::find(ThreadId id) const { return const_cast<Kernel*>(this)->find(id); }

bool Kernel::on_thread() const { return executing_ != 0 && Fiber::current() != nullptr; }

void Kernel::require_thread(const char* call) const {
    if (!on_thread()) throw RtosError(std::string(call) + " called outside an RTOS thread");
}

// A kernel call from a thread uses the configured processor time first (an SVC and
// its handler take time on the chip), then does its work.
void Kernel::enter_call(const char* call) {
    if (!initialized_) throw RtosError(std::string(call) + " before osKernelInitialize");
    // From the host between runs: a switch the kernel already decided has happened.
    if (!on_thread() && running_ && run_ && next_ != *run_) perform_switch();
    if (on_thread() && config_.call_cycles != 0) {
        call_cycles_total_ += config_.call_cycles;
        consume(config_.call_cycles);
    }
}

// After a call from a thread: if it made another thread run (or blocked this one),
// switch now; the call returns when this thread runs again.
void Kernel::after_call() {
    if (on_thread() && (!run_ || next_ != *run_)) Fiber::suspend();
}

std::uint16_t Kernel::ms2tick(std::uint32_t millisec) const {  // rt_ms2tick
    if (millisec == 0) return 0;
    if (millisec == kWaitForever) return kNoTimeout;
    if (millisec > 4'000'000u) return 0xFFFE;
    const std::uint32_t tick = (1000u * millisec + config_.tick_us - 1) / config_.tick_us;
    return static_cast<std::uint16_t>(std::min<std::uint32_t>(tick, 0xFFFE));
}

void Kernel::record(RtosOp op, ThreadId thread, int level, std::uint16_t object, std::uint32_t value) {
    platform_.record({.kind = TraceKind::Rtos,
                      .value = value,
                      .op = op,
                      .thread = static_cast<std::uint16_t>(thread),
                      .object = object,
                      .priority = static_cast<std::int16_t>(level)});
}

// ---- RTX lists (rt_List.c) ----

// Ordered by priority, highest first; equal priorities in arrival order.
void Kernel::put_prio(std::deque<ThreadId>& list, ThreadId id) {
    const int prio = tcb(id).prio;
    auto it = list.begin();
    while (it != list.end() && prio <= tcb(*it).prio) ++it;
    list.insert(it, id);
}

ThreadId Kernel::get_first(std::deque<ThreadId>& list) {
    const ThreadId id = list.front();
    list.pop_front();
    return id;
}

void Kernel::put_rdy_first(ThreadId id) { ready_.push_front(id); }

void Kernel::remove(std::deque<ThreadId>& list, ThreadId id) {
    if (auto it = std::find(list.begin(), list.end(), id); it != list.end()) list.erase(it);
}

int Kernel::rdy_prio() const { return ready_.empty() ? -1 : tcb(ready_.front()).prio; }

// A delay of `ticks` ends at the ticks-th system tick from now. Threads that end on
// the same tick are woken latest-first, as rt_put_dly inserts before equals.
void Kernel::put_dly(ThreadId id, std::uint32_t ticks) {
    Tcb& t = tcb(id);
    t.wake_tick = os_time_ + ticks;
    t.delayed = true;
    auto it = delayed_.begin();
    while (it != delayed_.end() && tcb(*it).wake_tick < t.wake_tick) ++it;
    delayed_.insert(it, id);
}

void Kernel::rmv_dly(ThreadId id) {
    Tcb& t = tcb(id);
    if (!t.delayed) return;
    remove(delayed_, id);
    t.delayed = false;
}

// rt_dec_dly, after os_time has advanced: wake every thread whose delay ended.
void Kernel::dec_dly() {
    while (!delayed_.empty() && tcb(delayed_.front()).wake_tick <= os_time_) {
        const ThreadId id = get_first(delayed_);
        Tcb& t = tcb(id);
        t.delayed = false;
        if (t.state == ThreadState::WaitingMutex) {  // timed out: leave the mutex queue
            remove(mutexes_.at(t.wait_mutex - 1)->waiters, id);
            t.wait_mutex = 0;
        }
        put_prio(ready_, id);
        t.state = ThreadState::Ready;
        record(RtosOp::Wake, id, t.prio);
    }
}

// rt_resort_prio: after a priority change, move the thread within its queue.
void Kernel::resort(ThreadId id) {
    Tcb& t = tcb(id);
    if (t.state == ThreadState::Ready) {
        remove(ready_, id);
        put_prio(ready_, id);
    } else if (t.state == ThreadState::WaitingMutex) {
        auto& waiters = mutexes_.at(t.wait_mutex - 1)->waiters;
        remove(waiters, id);
        put_prio(waiters, id);
    }
}

// ---- RTX tasks (rt_Task.c) ----

void Kernel::switch_req(ThreadId id) {
    next_ = id;
    tcb(id).state = ThreadState::Running;
}

void Kernel::dispatch(ThreadId next) {
    if (next == kNone) {
        switch_req(get_first(ready_));
        return;
    }
    Tcb& n = tcb(next);
    if (n.prio > tcb(*run_).prio) {
        put_rdy_first(*run_);
        tcb(*run_).state = ThreadState::Ready;
        switch_req(next);
    } else {
        n.state = ThreadState::Ready;
        put_prio(ready_, next);
    }
}

void Kernel::block(std::uint32_t ticks, ThreadState state) {
    if (ticks == 0) return;
    if (ticks < kNoTimeout) put_dly(*run_, ticks);
    tcb(*run_).state = state;
    switch_req(get_first(ready_));
}

void Kernel::task_prio(ThreadId id, int level) {
    Tcb& t = tcb(id);
    const int old = t.prio;
    t.prio = level;
    t.base = level;
    if (old != level) record(RtosOp::PriorityChange, id, level, 0, static_cast<std::uint32_t>(old));
    if (run_ && id == *run_) {
        if (rdy_prio() > level) {
            put_prio(ready_, id);
            t.state = ThreadState::Ready;
            dispatch(kNone);
        }
        return;
    }
    resort(id);
    if (t.state == ThreadState::Ready) dispatch(get_first(ready_));
}

// Mutexes a terminating thread owns pass to their first waiters (rt_tsk_delete).
void Kernel::release_owned(Tcb& t) {
    for (const MutexId m : t.owned) {
        Mutex& mx = *mutexes_.at(m - 1);
        if (!mx.waiters.empty()) {
            const ThreadId w = get_first(mx.waiters);
            Tcb& wt = tcb(w);
            wt.ret = {Status::Ok, 0};
            wt.wait_mutex = 0;
            rmv_dly(w);
            wt.state = ThreadState::Ready;
            put_prio(ready_, w);
            mx.level = 1;
            mx.owner = w;
            wt.owned.push_front(m);
            record(RtosOp::MutexAcquire, w, wt.prio, static_cast<std::uint16_t>(m));
        } else {
            mx.level = 0;
            mx.owner = kNone;
        }
    }
    t.owned.clear();
}

void Kernel::delete_task(ThreadId id) {
    Tcb& t = tcb(id);
    if (run_ && id == *run_) {
        t.state = ThreadState::Terminated;
        release_owned(t);
        record(RtosOp::Terminate, id, t.prio);
        run_.reset();
        dispatch(kNone);
        return;
    }
    if (t.state == ThreadState::WaitingMutex) remove(mutexes_.at(t.wait_mutex - 1)->waiters, id);
    else remove(ready_, id);
    rmv_dly(id);
    release_owned(t);
    t.state = ThreadState::Terminated;
    record(RtosOp::Terminate, id, t.prio);
    if (rdy_prio() > tcb(*run_).prio) {
        tcb(*run_).state = ThreadState::Ready;
        put_prio(ready_, *run_);
        dispatch(kNone);
    }
}

// rt_evt_set from a thread: wake the target if its wait is satisfied.
void Kernel::evt_set(ThreadId id, std::uint16_t flags) {
    Tcb& t = tcb(id);
    t.events |= flags;
    const std::uint16_t wanted = t.waits;
    if (t.state != ThreadState::WaitingSignal) return;
    if (t.wait_and ? (t.events & wanted) != wanted : (t.events & wanted) == 0) return;
    if (!t.wait_and) t.waits &= t.events;
    t.events &= static_cast<std::uint16_t>(~wanted);
    rmv_dly(id);
    t.state = ThreadState::Ready;
    t.ret = {Status::EventSignal, t.waits};
    record(RtosOp::SignalWake, id, t.prio, 0, t.waits);
    dispatch(id);
}

// ---- kernel control ----

Status Kernel::initialize() {
    if (platform_.in_interrupt()) return Status::ErrorIsr;
    if (!initialized_) {
        auto idle = std::make_unique<Tcb>();
        idle->name = "os_idle_demon";
        idle->state = ThreadState::Running;
        threads_.push_back(std::move(idle));
        run_ = 0;
        next_ = 0;
        accounted_to_ = platform_.now();
    }
    tcb(*run_).prio = kLevelBoost;  // creating threads never preempts the caller
    if (!initialized_) {
        initialized_ = true;
        timer_thread_ = create_thread([this] { timer_thread_body(); }, config_.timer_priority, "osTimerThread");
    }
    running_ = false;
    return Status::Ok;
}

Status Kernel::start() {
    if (platform_.in_interrupt()) return Status::ErrorIsr;
    if (!initialized_) throw RtosError("osKernelStart before osKernelInitialize");
    if (running_) return Status::Ok;
    Tcb& r = tcb(*run_);
    record(RtosOp::KernelStart, r.id, r.base);
    // rt_sys_start: the tick is programmed (again) each time the kernel starts.
    platform_.start_tick(config_.tick_period, [this] { tick(); });
    tick_started_ = true;
    running_ = true;
    r.prio = r.base;  // rt_tsk_prio(0, prio_base): drop the boost, yield to anything higher
    if (rdy_prio() > r.prio) {
        put_prio(ready_, r.id);
        r.state = ThreadState::Ready;
        dispatch(kNone);
    }
    after_call();
    return Status::Ok;
}

void Kernel::start_main(std::function<void()> main) {
    initialize();
    create_thread(std::move(main), kPriorityNormal, "main");
    start();
}

// ---- threads ----

ThreadId Kernel::create_thread(std::function<void()> entry, int priority, std::string name) {
    if (platform_.in_interrupt()) return 0;
    enter_call("osThreadCreate");
    if (priority < kPriorityIdle || priority > kPriorityRealtime || !entry) return 0;
    auto t = std::make_unique<Tcb>();
    t->id = static_cast<ThreadId>(threads_.size());
    t->name = name.empty() ? "T" + std::to_string(t->id) : std::move(name);
    t->entry = std::move(entry);
    t->base = t->prio = level_of(priority);
    const ThreadId id = t->id;
    threads_.push_back(std::move(t));
    record(RtosOp::Create, id, tcb(id).prio);
    dispatch(id);
    after_call();
    return id;
}

ThreadId Kernel::current_thread() const {
    if (!initialized_ || platform_.in_interrupt() || !run_ || *run_ == 0) return 0;
    return on_thread() ? *run_ : 0;
}

Status Kernel::terminate(ThreadId id) {
    if (platform_.in_interrupt()) return Status::ErrorIsr;
    enter_call("osThreadTerminate");
    if (find(id) == nullptr) return Status::ErrorParameter;
    delete_task(id);
    after_call();
    return Status::Ok;
}

Status Kernel::yield() {
    if (platform_.in_interrupt()) return Status::ErrorIsr;
    require_thread("osThreadYield");
    enter_call("osThreadYield");
    Tcb& r = tcb(*run_);
    if (!ready_.empty() && tcb(ready_.front()).prio == r.prio) {  // rt_tsk_pass
        const ThreadId other = get_first(ready_);
        put_prio(ready_, r.id);
        r.state = ThreadState::Ready;
        record(RtosOp::Yield, r.id, r.prio);
        switch_req(other);
    }
    after_call();
    return Status::Ok;
}

Status Kernel::set_priority(ThreadId id, int priority) {
    if (platform_.in_interrupt()) return Status::ErrorIsr;
    enter_call("osThreadSetPriority");
    if (find(id) == nullptr) return Status::ErrorParameter;
    if (priority < kPriorityIdle || priority > kPriorityRealtime) return Status::ErrorValue;
    task_prio(id, level_of(priority));
    after_call();
    return Status::Ok;
}

int Kernel::priority(ThreadId id) const {
    const Tcb* t = find(id);
    return t == nullptr ? kPriorityError : t->prio - 1 + kPriorityIdle;
}

Status Kernel::delay(std::uint32_t millisec) {
    if (platform_.in_interrupt()) return Status::ErrorIsr;
    require_thread("osDelay");
    enter_call("osDelay");
    if (millisec == 0) return Status::Ok;
    const std::uint16_t ticks = ms2tick(millisec);
    Tcb& r = tcb(*run_);
    r.ret = {Status::EventTimeout, 0};
    record(RtosOp::Delay, r.id, r.prio, 0, ticks);
    block(ticks, ThreadState::Delayed);
    after_call();
    return tcb(r.id).ret.status;
}

// ---- signals (rt_Event.c, svcSignal*) ----

std::int32_t Kernel::signal_set(ThreadId id, std::int32_t signals) {
    constexpr auto kError = static_cast<std::int32_t>(0x80000000u);
    if (platform_.in_interrupt()) {  // isrSignalSet: handled after the interrupts (PendSV)
        Tcb* t = find(id);
        if (t == nullptr || (static_cast<std::uint32_t>(signals) & 0xFFFF0000u)) return kError;
        record(RtosOp::SignalSet, kIsrThread, 0, static_cast<std::uint16_t>(id), static_cast<std::uint32_t>(signals));
        isr_signals_.emplace_back(id, static_cast<std::uint16_t>(signals));
        return t->events;
    }
    enter_call("osSignalSet");
    Tcb* t = find(id);
    if (t == nullptr || (static_cast<std::uint32_t>(signals) & 0xFFFF0000u)) return kError;
    const std::int32_t previous = t->events;
    const ThreadId caller = run_ ? *run_ : 0;
    record(RtosOp::SignalSet, caller, run_ ? tcb(caller).prio : 0, static_cast<std::uint16_t>(id),
           static_cast<std::uint32_t>(signals));
    evt_set(id, static_cast<std::uint16_t>(signals));
    after_call();
    return previous;
}

std::int32_t Kernel::signal_clear(ThreadId id, std::int32_t signals) {
    constexpr auto kError = static_cast<std::int32_t>(0x80000000u);
    if (platform_.in_interrupt()) return kError;
    enter_call("osSignalClear");
    Tcb* t = find(id);
    if (t == nullptr || (static_cast<std::uint32_t>(signals) & 0xFFFF0000u)) return kError;
    const std::int32_t previous = t->events;
    t->events &= static_cast<std::uint16_t>(~signals);
    const ThreadId caller = run_ ? *run_ : 0;
    record(RtosOp::SignalClear, caller, run_ ? tcb(caller).prio : 0, static_cast<std::uint16_t>(id),
           static_cast<std::uint32_t>(signals));
    after_call();
    return previous;
}

Event Kernel::signal_wait(std::int32_t signals, std::uint32_t millisec) {
    if (platform_.in_interrupt()) return {Status::ErrorIsr, 0};
    require_thread("osSignalWait");
    enter_call("osSignalWait");
    if (static_cast<std::uint32_t>(signals) & 0xFFFF0000u) return {Status::ErrorValue, 0};
    Tcb& r = tcb(*run_);
    const bool all = signals != 0;  // wait for all given flags, or for any flag
    const auto wanted = all ? static_cast<std::uint16_t>(signals) : std::uint16_t{0xFFFF};
    if (all ? (r.events & wanted) == wanted : (r.events & wanted) != 0) {
        if (!all) r.waits = r.events & wanted;
        r.events &= static_cast<std::uint16_t>(~wanted);
        return {Status::EventSignal, all ? signals : r.waits};
    }
    const std::uint16_t ticks = ms2tick(millisec);
    r.ret = {millisec != 0 ? Status::EventTimeout : Status::Ok, 0};
    if (ticks == 0) return r.ret;
    r.wait_and = all;
    r.waits = wanted;
    record(RtosOp::SignalWait, r.id, r.prio, 0, static_cast<std::uint32_t>(signals));
    block(ticks, ThreadState::WaitingSignal);
    after_call();
    return tcb(r.id).ret;
}

// ---- mutexes (rt_Mutex.c) ----

MutexId Kernel::create_mutex() {
    if (platform_.in_interrupt()) return 0;
    enter_call("osMutexCreate");
    auto m = std::make_unique<Mutex>();
    m->id = static_cast<MutexId>(mutexes_.size() + 1);
    const MutexId id = m->id;
    mutexes_.push_back(std::move(m));
    return id;
}

Status Kernel::mutex_wait(MutexId id, std::uint32_t millisec) {
    if (platform_.in_interrupt()) return Status::ErrorIsr;
    require_thread("osMutexWait");
    enter_call("osMutexWait");
    if (id == 0 || id > mutexes_.size() || !mutexes_[id - 1]->valid) return Status::ErrorParameter;
    Mutex& m = *mutexes_[id - 1];
    Tcb& r = tcb(*run_);
    if (m.level == 0) {
        m.owner = r.id;
        r.owned.push_front(id);
        m.level = 1;
        record(RtosOp::MutexAcquire, r.id, r.prio, static_cast<std::uint16_t>(id));
        return Status::Ok;
    }
    if (m.owner == r.id) {
        if (m.level == 0xFFFF) return Status::ErrorResource;
        ++m.level;
        return Status::Ok;
    }
    const std::uint16_t ticks = ms2tick(millisec);
    if (ticks == 0) return millisec != 0 ? Status::ErrorTimeoutResource : Status::ErrorResource;
    Tcb& owner = tcb(m.owner);
    if (owner.prio < r.prio) {  // priority inheritance
        owner.prio = r.prio;
        record(RtosOp::PriorityInherit, owner.id, owner.prio, static_cast<std::uint16_t>(id));
        resort(owner.id);
    }
    put_prio(m.waiters, r.id);
    r.wait_mutex = id;
    r.ret = {Status::ErrorTimeoutResource, 0};
    record(RtosOp::MutexBlock, r.id, r.prio, static_cast<std::uint16_t>(id));
    block(ticks, ThreadState::WaitingMutex);
    after_call();
    return tcb(r.id).ret.status;
}

Status Kernel::mutex_release(MutexId id) {
    if (platform_.in_interrupt()) return Status::ErrorIsr;
    require_thread("osMutexRelease");
    enter_call("osMutexRelease");
    if (id == 0 || id > mutexes_.size() || !mutexes_[id - 1]->valid) return Status::ErrorParameter;
    Mutex& m = *mutexes_[id - 1];
    Tcb& r = tcb(*run_);
    if (m.level == 0 || m.owner != r.id) return Status::ErrorResource;
    if (--m.level != 0) return Status::Ok;
    remove_mutex_from_owner(r, id);
    record(RtosOp::MutexRelease, r.id, r.prio, static_cast<std::uint16_t>(id));
    restore_priority(r, id);
    if (!m.waiters.empty()) {
        const ThreadId w = get_first(m.waiters);
        Tcb& wt = tcb(w);
        wt.ret = {Status::Ok, 0};
        wt.wait_mutex = 0;
        rmv_dly(w);
        m.level = 1;
        m.owner = w;
        wt.owned.push_front(id);
        record(RtosOp::MutexAcquire, w, wt.prio, static_cast<std::uint16_t>(id));
        if (r.prio >= rdy_prio()) {
            dispatch(w);
        } else {
            put_prio(ready_, r.id);
            put_prio(ready_, w);
            r.state = ThreadState::Ready;
            wt.state = ThreadState::Ready;
            dispatch(kNone);
        }
    } else if (rdy_prio() > r.prio) {
        put_prio(ready_, r.id);
        r.state = ThreadState::Ready;
        dispatch(kNone);
    }
    after_call();
    return Status::Ok;
}

Status Kernel::mutex_delete(MutexId id) {
    if (platform_.in_interrupt()) return Status::ErrorIsr;
    enter_call("osMutexDelete");
    if (id == 0 || id > mutexes_.size() || !mutexes_[id - 1]->valid) return Status::ErrorParameter;
    Mutex& m = *mutexes_[id - 1];
    if (m.level != 0) {
        Tcb& owner = tcb(m.owner);
        remove_mutex_from_owner(owner, id);
        const int before = owner.prio;
        restore_priority(owner, id);
        if (owner.prio != before && (!run_ || owner.id != *run_)) resort(owner.id);
    }
    while (!m.waiters.empty()) {
        const ThreadId w = get_first(m.waiters);
        Tcb& wt = tcb(w);
        wt.ret = {Status::Ok, 0};
        wt.wait_mutex = 0;
        rmv_dly(w);
        wt.state = ThreadState::Ready;
        put_prio(ready_, w);
    }
    if (run_ && !ready_.empty() && tcb(ready_.front()).prio > tcb(*run_).prio) {
        put_prio(ready_, *run_);
        tcb(*run_).state = ThreadState::Ready;
        dispatch(kNone);
    }
    m.valid = false;
    m.level = 0;
    m.owner = kNone;
    after_call();
    return Status::Ok;
}

void Kernel::remove_mutex_from_owner(Tcb& t, MutexId id) {
    if (auto it = std::find(t.owned.begin(), t.owned.end(), id); it != t.owned.end()) t.owned.erase(it);
}

// The owner's priority after giving up `released`: its base, or the highest first
// waiter of a mutex it still owns.
void Kernel::restore_priority(Tcb& t, MutexId released) {
    int prio = t.base;
    for (const MutexId m : t.owned) {
        const Mutex& mx = *mutexes_.at(m - 1);
        if (!mx.waiters.empty() && tcb(mx.waiters.front()).prio > prio) prio = tcb(mx.waiters.front()).prio;
    }
    if (t.prio != prio) {
        t.prio = prio;
        record(RtosOp::PriorityRestore, t.id, prio, static_cast<std::uint16_t>(released));
    }
}

// ---- virtual timers (rt_CMSIS.c timer management) ----

TimerId Kernel::create_timer(std::function<void()> callback, bool periodic) {
    if (platform_.in_interrupt()) return 0;
    enter_call("osTimerCreate");
    if (!callback || timer_thread_ == 0) return 0;
    auto t = std::make_unique<Timer>();
    t->id = static_cast<TimerId>(timers_.size() + 1);
    t->callback = std::move(callback);
    t->periodic = periodic;
    const TimerId id = t->id;
    timers_.push_back(std::move(t));
    return id;
}

void Kernel::timer_insert(TimerId id, std::uint32_t tcnt) {
    Timer& pt = *timers_.at(id - 1);
    auto it = timer_active_.begin();
    while (it != timer_active_.end()) {
        Timer& p = *timers_.at(*it - 1);
        if (tcnt < p.tcnt) break;
        tcnt -= p.tcnt;
        ++it;
    }
    pt.tcnt = tcnt;
    if (it != timer_active_.end()) timers_.at(*it - 1)->tcnt -= tcnt;
    timer_active_.insert(it, id);
}

bool Kernel::timer_remove(TimerId id) {
    auto it = std::find(timer_active_.begin(), timer_active_.end(), id);
    if (it == timer_active_.end()) return false;
    const std::uint32_t tcnt = timers_.at(id - 1)->tcnt;
    it = timer_active_.erase(it);
    if (it != timer_active_.end()) timers_.at(*it - 1)->tcnt += tcnt;
    return true;
}

Status Kernel::timer_start(TimerId id, std::uint32_t millisec) {
    if (platform_.in_interrupt()) return Status::ErrorIsr;
    enter_call("osTimerStart");
    if (id == 0 || id > timers_.size()) return Status::ErrorParameter;
    if (millisec == 0) return Status::ErrorValue;
    Timer& t = *timers_[id - 1];
    const auto tcnt = static_cast<std::uint32_t>((1000u * static_cast<std::uint64_t>(millisec) + config_.tick_us - 1) /
                                                 config_.tick_us);
    switch (t.state) {
    case Timer::State::Running:
        if (!timer_remove(id)) return Status::ErrorResource;
        break;  // RTX keeps the old period (icnt) when restarting a running timer
    case Timer::State::Stopped:
        t.state = Timer::State::Running;
        t.icnt = tcnt;
        break;
    case Timer::State::Invalid: return Status::ErrorResource;
    }
    timer_insert(id, tcnt);
    record(RtosOp::TimerStart, run_ ? *run_ : 0, run_ ? tcb(*run_).prio : 0, static_cast<std::uint16_t>(id), tcnt);
    after_call();
    return Status::Ok;
}

Status Kernel::timer_stop(TimerId id) {
    if (platform_.in_interrupt()) return Status::ErrorIsr;
    enter_call("osTimerStop");
    if (id == 0 || id > timers_.size()) return Status::ErrorParameter;
    Timer& t = *timers_[id - 1];
    if (t.state != Timer::State::Running) return Status::ErrorResource;
    t.state = Timer::State::Stopped;
    if (!timer_remove(id)) return Status::ErrorResource;
    record(RtosOp::TimerStop, run_ ? *run_ : 0, run_ ? tcb(*run_).prio : 0, static_cast<std::uint16_t>(id));
    return Status::Ok;
}

Status Kernel::timer_delete(TimerId id) {
    if (platform_.in_interrupt()) return Status::ErrorIsr;
    enter_call("osTimerDelete");
    if (id == 0 || id > timers_.size()) return Status::ErrorParameter;
    Timer& t = *timers_[id - 1];
    switch (t.state) {
    case Timer::State::Running: timer_remove(id); break;
    case Timer::State::Stopped: break;
    case Timer::State::Invalid: return Status::ErrorResource;
    }
    t.state = Timer::State::Invalid;
    return Status::Ok;
}

// sysTimerTick: expired timers become messages for the timer thread.
void Kernel::timer_tick() {
    if (timer_active_.empty()) return;
    --timers_.at(timer_active_.front() - 1)->tcnt;
    while (!timer_active_.empty() && timers_.at(timer_active_.front() - 1)->tcnt == 0) {
        const TimerId id = timer_active_.front();
        timer_active_.pop_front();
        Timer& t = *timers_.at(id - 1);
        if (timer_messages_.size() + pending_messages_.size() >= config_.timer_queue)
            throw RtosError("timer callback queue overflow (OS_TIMERCBQS = " + std::to_string(config_.timer_queue) +
                            "): the timer thread is not keeping up");
        pending_messages_.push_back(id);
        record(RtosOp::TimerFire, timer_thread_, tcb(timer_thread_).prio, static_cast<std::uint16_t>(id));
        if (t.periodic) timer_insert(id, t.icnt);
        else t.state = Timer::State::Stopped;
    }
}

// osTimerThread: wait for a message, call the timer's function, repeat.
void Kernel::timer_thread_body() {
    for (;;) {
        enter_call("osMessageGet");
        Tcb& self = tcb(timer_thread_);
        TimerId id = 0;
        if (!timer_messages_.empty()) {
            id = timer_messages_.front();
            timer_messages_.pop_front();
        } else {
            self.message = 0;
            block(kNoTimeout, ThreadState::WaitingMessage);
            after_call();
            id = self.message;
        }
        Timer& t = *timers_.at(id - 1);
        record(RtosOp::TimerCallback, timer_thread_, self.prio, static_cast<std::uint16_t>(id));
        if (t.callback) t.callback();
    }
}

// ---- interrupts ----

// rt_systick, at each SysTick interrupt. "run" is the thread the interrupt
// returns to: next_, since switches take effect on the way out of the kernel.
void Kernel::tick() {
    const ThreadId cur = next_;
    tcb(cur).state = ThreadState::Ready;
    put_rdy_first(cur);
    if (config_.round_robin) {  // rt_chk_robin
        const ThreadId head = ready_.front();
        if (!robin_valid_ || robin_task_ != head) {
            robin_task_ = head;
            robin_valid_ = true;
            robin_time_ = static_cast<std::uint16_t>(os_time_ + config_.round_robin_ticks - 1);
        }
        if (robin_time_ == static_cast<std::uint16_t>(os_time_)) {
            robin_valid_ = false;
            put_prio(ready_, get_first(ready_));
        }
    }
    ++os_time_;
    dec_dly();
    timer_tick();
    switch_req(get_first(ready_));
}

// rt_pop_req, after interrupts that queued work for the kernel (PendSV).
void Kernel::post_service() {
    if (isr_signals_.empty() && pending_messages_.empty()) return;
    const ThreadId cur = next_;
    tcb(cur).state = ThreadState::Ready;
    put_rdy_first(cur);
    for (const auto& [id, flags] : isr_signals_) {  // rt_evt_psh
        Tcb& t = tcb(id);
        if (t.state == ThreadState::Terminated) continue;
        t.events |= flags;
        const std::uint16_t wanted = t.waits;
        if (t.state != ThreadState::WaitingSignal) continue;
        if (t.wait_and ? (t.events & wanted) != wanted : (t.events & wanted) == 0) continue;
        if (!t.wait_and) t.waits &= t.events;
        t.events &= static_cast<std::uint16_t>(~wanted);
        rmv_dly(id);
        t.state = ThreadState::Ready;
        t.ret = {Status::EventSignal, t.waits};
        record(RtosOp::SignalWake, id, t.prio, 0, t.waits);
        put_prio(ready_, id);
    }
    isr_signals_.clear();
    for (const TimerId id : pending_messages_) {  // rt_mbx_psh to the timer thread's queue
        Tcb& tt = tcb(timer_thread_);
        if (tt.state == ThreadState::WaitingMessage) {
            tt.message = id;
            tt.ret = {Status::EventMessage, 0};
            rmv_dly(timer_thread_);
            tt.state = ThreadState::Ready;
            put_prio(ready_, timer_thread_);
        } else {
            timer_messages_.push_back(id);
        }
    }
    pending_messages_.clear();
    switch_req(get_first(ready_));
}

// ---- execution ----

void Kernel::consume(std::uint64_t cycles) {
    require_thread("consume");
    if (cycles == 0) return;
    tcb(*run_).consume_left += cycles;
    Fiber::suspend();
}

void Kernel::preemption_point() {
    if (on_thread() && initialized_ && (!run_ || next_ != *run_)) Fiber::suspend();
}

void Kernel::abandon_thread(std::exception_ptr error) {
    if (!on_thread()) std::rethrow_exception(error);
    fault_ = std::move(error);
    Fiber::suspend();
    std::abort();  // never resumed: run_until rethrows the fault instead
}

// Attribute the time since the last call to the thread that was running.
void Kernel::account() {
    const std::uint64_t now = platform_.now();
    if (now <= accounted_to_) return;
    const ThreadId who = run_ ? *run_ : 0;
    tcb(who).run_cycles += now - accounted_to_;
    if (!timeline_.empty() && timeline_.back().thread == who && timeline_.back().end == accounted_to_)
        timeline_.back().end = now;
    else
        timeline_.push_back({accounted_to_, now, who});
    accounted_to_ = now;
}

void Kernel::perform_switch() {
    account();
    if (run_ && *run_ != next_) {
        Tcb& old = tcb(*run_);
        if (old.state == ThreadState::Ready && old.id != 0) record(RtosOp::Preempt, old.id, old.prio);
    }
    run_ = next_;
    Tcb& n = tcb(next_);
    n.state = ThreadState::Running;
    record(RtosOp::Run, n.id, n.prio);
}

void Kernel::start_fiber(Tcb& t) {
    const ThreadId id = t.id;
    t.fiber = std::make_unique<Fiber>(
        [this, id] {
            try {
                tcb(id).entry();
                // Returning from the thread function terminates it (osThreadExit).
                delete_task(id);
            } catch (...) {
                fault_ = std::current_exception();
            }
        },
        config_.stack_bytes);
}

void Kernel::run_until(std::uint64_t cycle) {
    if (on_thread() || platform_.in_interrupt()) throw RtosError("run_until from inside the simulation");
    if (!running_) {  // no scheduler yet: plain time
        if (cycle > platform_.now()) platform_.advance(cycle - platform_.now());
        return;
    }
    for (;;) {
        if (fault_) std::rethrow_exception(std::exchange(fault_, nullptr));
        if (!run_ || next_ != *run_) perform_switch();
        Tcb& r = tcb(*run_);
        const std::uint64_t now = platform_.now();
        if (r.id != 0 && r.consume_left == 0) {
            if (now != last_switch_time_) {
                last_switch_time_ = now;
                zero_time_switches_ = 0;
            } else if (++zero_time_switches_ > config_.max_zero_time_switches) {
                throw RtosError("threads keep running without virtual time passing at t=" + std::to_string(now) +
                                " (a loop of kernel calls with call_cycles = 0, or no consume())");
            }
            if (!r.fiber) start_fiber(r);
            executing_ = r.id;
            r.fiber->resume();
            executing_ = 0;
            continue;
        }
        if (now >= cycle) break;
        std::uint64_t step = cycle - now;
        if (const std::uint64_t e = platform_.cycles_to_next_event(); e != 0 && e < step) step = e;
        if (r.id != 0 && r.consume_left < step) step = r.consume_left;
        const ThreadId who = r.id;
        platform_.advance(step);
        account();
        if (who != 0) tcb(who).consume_left -= step;
    }
    account();
}

// ---- inspection ----

std::vector<ThreadInfo> Kernel::threads() const {
    std::vector<ThreadInfo> out;
    for (const auto& p : threads_) out.push_back(*thread(p->id));
    return out;
}

std::optional<ThreadInfo> Kernel::thread(ThreadId id) const {
    if (id >= threads_.size()) return std::nullopt;
    const Tcb& t = *threads_[id];
    ThreadInfo info;
    info.id = t.id;
    info.name = t.name;
    info.state = t.state;
    info.base_level = t.base;
    info.level = t.prio;
    info.signals = t.events;
    info.waiting_for = t.state == ThreadState::WaitingSignal ? t.waits : std::uint16_t{0};
    if (t.delayed) info.wake_tick = t.wake_tick;
    info.waiting_mutex = t.wait_mutex;
    info.run_cycles = t.run_cycles;
    if (run_ && *run_ == id && platform_.now() > accounted_to_) info.run_cycles += platform_.now() - accounted_to_;
    return info;
}

ThreadId Kernel::running_thread() const { return run_ ? *run_ : 0; }

std::vector<MutexInfo> Kernel::mutexes() const {
    std::vector<MutexInfo> out;
    for (const auto& m : mutexes_) {
        if (!m->valid) continue;
        out.push_back({m->id, m->level ? m->owner : 0, m->level, {m->waiters.begin(), m->waiters.end()}});
    }
    return out;
}

std::vector<TimerInfo> Kernel::timers() const {
    std::vector<TimerInfo> out;
    for (const auto& t : timers_) {
        if (t->state == Timer::State::Invalid) continue;
        out.push_back({t->id, t->periodic, t->state == Timer::State::Running, t->icnt});
    }
    return out;
}

std::uint64_t Kernel::idle_cycles() const { return threads_.empty() ? 0 : thread(0)->run_cycles; }

void Kernel::set_thread_name(ThreadId id, std::string name) { tcb(id).name = std::move(name); }

}  // namespace latasim::rtos
