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

    bool operator==(const TraceEvent&) const = default;
};

class Trace {
public:
    // Stamps the next sequence number and the given virtual time, unless recording
    // is off.
    void record(TraceEvent event, std::uint64_t cycles) {
        if (!enabled_) return;
        event.seq = ++last_seq_;
        event.cycles = cycles;
        events_.push_back(event);
    }

    const std::vector<TraceEvent>& events() const { return events_; }

    // Off: nothing is recorded; the model behaves identically either way.
    void set_enabled(bool enabled) { enabled_ = enabled; }
    bool enabled() const { return enabled_; }

private:
    std::vector<TraceEvent> events_;
    std::uint64_t last_seq_ = 0;
    bool enabled_ = true;
};

// One line, e.g. "#12   t=999999     write32 FIO1SET   0x10000000",
// "#13   t=999999     led     LED0      ON", "#14   t=2500000    input   P1.23     low".
// Registers are named as in LPC17xx.h; t is in core clock cycles.
std::string to_string(const TraceEvent& event);

}  // namespace latasim
