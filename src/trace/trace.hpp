#pragma once
// A deterministic record of how firmware and the outside world touched the
// modelled hardware. Each Lpc1768 owns one; there is no global trace.
//
// Events are structured; to_string() only formats them. Sequence numbers start at
// 1 per machine and increase by one per event, so identical runs give identical
// traces: nothing depends on time, addresses of host objects or thread identity.
#include <cstdint>
#include <string>
#include <vector>

namespace latasim {

enum class TraceKind : std::uint8_t {
    Read,   // an MMIO load: address, width, value returned
    Write,  // an MMIO store: address, width, value stored
    Input,  // a board input changed a pin's external level: port, pin, value = level
    Led,    // a board LED changed what it shows: led, value = mcb1700::LedState
};

struct TraceEvent {
    std::uint64_t seq = 0;
    TraceKind kind = TraceKind::Read;
    std::uint32_t address = 0;  // Read, Write
    unsigned width = 0;         // Read, Write: access size in bytes (1, 2 or 4)
    std::uint32_t value = 0;
    unsigned port = 0;  // Input
    unsigned pin = 0;   // Input
    unsigned led = 0;   // Led

    bool operator==(const TraceEvent&) const = default;
};

class Trace {
public:
    // Stamps the next sequence number, unless recording is off.
    void record(TraceEvent event) {
        if (!enabled_) return;
        event.seq = ++last_seq_;
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

// One line, e.g. "#12 write32 FIO1SET 0x10000000", "#13 led LED0 ON",
// "#14 input P1.23 low". Registers are named as in LPC17xx.h.
std::string to_string(const TraceEvent& event);

}  // namespace latasim
