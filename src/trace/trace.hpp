#pragma once
// A deterministic record of how firmware and the outside world touched the
// modelled hardware. Each Lpc1768 owns one; there is no global trace.
//
// Events are structured; to_string() only formats them. Sequence numbers start at
// 1 per machine and increase by one per event: they are the exact order. Each
// event also carries the machine's virtual time (core clock cycles) when it
// happened; events without time passing between them share a time. Identical runs
// give identical traces: nothing depends on wall-clock time, addresses of host
// objects or thread identity.
#include <cstdint>
#include <string>
#include <vector>

namespace latasim {

enum class TraceKind : std::uint8_t {
    Read,   // an MMIO load: address, width, value returned
    Write,  // an MMIO store: address, width, value stored
    Input,  // a board input changed a pin's external level: port, pin, value = level
    Led,    // a board LED changed what it shows: led, value = mcb1700::LedState
    Interrupt,  // an exception: irq; value = kInterruptPend, kInterruptEnter or kInterruptExit
    TimerMatch,  // timer match(es) set IR flags: irq = the timer's IRQ, value = the IR bits
    AdcConversion,  // an A/D conversion completed: pin = channel, value = 12-bit result
    Display,  // a GLCD controller register write: address = index, value = data;
              // for GRAM (index 0x22) one event per burst, value = pixels written
    Rtos,     // a scheduler or RTOS object event: op = RtosOp, thread (0 = the idle
              // demon), object, priority and value as RtosOp says
};

// TraceKind::Rtos `thread` for an event raised by an interrupt handler.
inline constexpr std::uint16_t kIsrThread = 0xFFFF;

// TraceKind::Rtos operations. `thread` is the thread the event is about; priorities
// are RTX levels (0 idle demon, 1-7 osPriorityIdle-osPriorityRealtime).
enum class RtosOp : std::uint16_t {
    KernelStart,      // priority: of the thread that started the kernel
    Create,           // thread created; priority
    Run,              // thread starts running; priority
    Preempt,          // thread stopped running, still ready; priority
    Yield,            // thread passed the processor to an equal-priority thread
    Delay,            // thread blocked for value ticks
    Wake,             // thread became ready after a delay or a timed-out wait
    Terminate,        // thread terminated
    SignalSet,        // object = the target thread, value = flags set
    SignalClear,      // object = the target thread, value = flags cleared
    SignalWait,       // thread blocked waiting for value (0 = any flag)
    SignalWake,       // thread woken by value
    MutexAcquire,     // object = mutex
    MutexBlock,       // object = mutex
    MutexRelease,     // object = mutex
    PriorityChange,   // priority = new level, value = old level
    PriorityInherit,  // thread raised to priority by a waiter on object (mutex)
    PriorityRestore,  // thread lowered to priority after releasing object (mutex)
    TimerStart,       // object = timer, value = ticks
    TimerStop,        // object = timer
    TimerFire,        // object = timer: its period elapsed
    TimerCallback,    // object = timer: the timer thread calls its function
};

// TraceKind::Interrupt values: became pending, handler entered, handler returned.
inline constexpr std::uint32_t kInterruptPend = 0;
inline constexpr std::uint32_t kInterruptEnter = 1;
inline constexpr std::uint32_t kInterruptExit = 2;

struct TraceEvent {
    std::uint64_t seq = 0;
    std::uint64_t cycles = 0;  // virtual time, core clock cycles since the machine started
    TraceKind kind = TraceKind::Read;
    std::uint32_t address = 0;  // Read, Write
    unsigned width = 0;         // Read, Write: access size in bytes (1, 2 or 4)
    std::uint32_t value = 0;
    unsigned port = 0;  // Input
    unsigned pin = 0;   // Input
    unsigned led = 0;   // Led
    int irq = 0;        // Interrupt: CMSIS IRQ number, SysTick = -1
    RtosOp op = RtosOp::KernelStart;  // Rtos
    std::uint16_t thread = 0;         // Rtos: stable thread number, 0 = idle demon
    std::uint16_t object = 0;         // Rtos: stable mutex/timer/thread number
    std::int16_t priority = 0;        // Rtos: RTX priority level

    bool operator==(const TraceEvent&) const = default;
};

// Retention: MMIO accesses (Read, Write) are the bulk of a trace; firmware that
// polls a register makes millions a second. At most mmio_capacity() of them are
// kept: when there are more, the oldest half are dropped in one step, and
// dropped() counts them. Every other event (inputs, LEDs, interrupts, timer
// matches, conversions, display writes, RTOS events) is always kept: what the
// accesses did stays in the trace. Sequence numbers are never reused, so the gaps
// show where accesses were dropped.
inline constexpr std::size_t kDefaultMmioCapacity = 1'000'000;

class Trace {
public:
    // Stamps the next sequence number and the given virtual time, unless recording
    // is off.
    void record(TraceEvent event, std::uint64_t cycles) {
        if (!enabled_) return;
        event.seq = ++last_seq_;
        event.cycles = cycles;
        events_.push_back(event);
        if ((event.kind == TraceKind::Read || event.kind == TraceKind::Write) && ++mmio_ > mmio_capacity_)
            drop_oldest_mmio();
    }

    const std::vector<TraceEvent>& events() const { return events_; }

    // Off: nothing is recorded; the model behaves identically either way.
    void set_enabled(bool enabled) { enabled_ = enabled; }
    bool enabled() const { return enabled_; }

    std::size_t mmio_capacity() const { return mmio_capacity_; }
    void set_mmio_capacity(std::size_t capacity);  // at least 2; applies from the next access
    std::uint64_t dropped() const { return dropped_; }  // MMIO accesses no longer held

private:
    void drop_oldest_mmio();

    std::vector<TraceEvent> events_;
    std::uint64_t last_seq_ = 0;
    bool enabled_ = true;
    std::size_t mmio_ = 0;  // MMIO accesses held
    std::size_t mmio_capacity_ = kDefaultMmioCapacity;
    std::uint64_t dropped_ = 0;
};

// An event's display fields: its category ("mmio", "usb/audio", "input", "led",
// "interrupt", "timer", "adc", "glcd", "rtos"), operation ("write32", "irq",
// "rtos" ...), subject (a register, pin, LED, IRQ or thread) and value, as
// to_string() lays them out.
struct TraceParts {
    std::string category;
    std::string operation;
    std::string subject;
    std::string value;
};
TraceParts describe(const TraceEvent& event);

// One line, e.g. "#12   t=999999     write32 FIO1SET   0x10000000",
// "#13   t=999999     led     LED0      ON", "#14   t=2500000    input   P1.23     low".
// Registers are named as in LPC17xx.h; t is in core clock cycles.
std::string to_string(const TraceEvent& event);

}  // namespace latasim
