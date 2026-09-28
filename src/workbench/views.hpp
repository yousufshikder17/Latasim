#pragma once
// View data for the desktop, computed from a session's model state without
// changing it: register groups for inspection and the scheduler timeline. Kept
// separate from Qt so it can be tested headless and reused by other front ends.
#include <cstdint>
#include <string>
#include <vector>

#include "rtos/kernel.hpp"

namespace latasim::workbench {

class Session;

struct RegisterEntry {
    std::string name;
    std::uint32_t address = 0;
};

struct RegisterGroup {
    std::string name;
    std::vector<RegisterEntry> registers;
};

// Modelled registers by block (GPIO, SysTick, NVIC, Timers, ADC, PINCON/EINT, USB,
// DAC). Values come from Lpc1768::peek32, which never has side effects: reading
// STCTRL, ADGDR or USBRxData this way changes nothing.
const std::vector<RegisterGroup>& register_groups();
std::uint32_t register_value(const Session& session, std::uint32_t address);

// A point on the timeline: an interrupt taken, or an RTOS event on a thread.
struct TimelineMarker {
    std::uint64_t cycles = 0;
    std::string label;
};

struct TimelineRow {
    rtos::ThreadId thread = 0;
    std::string name;
    std::vector<rtos::Interval> intervals;  // when it ran
    std::vector<TimelineMarker> markers;    // signals, mutexes, priority changes, timers
};

struct Timeline {
    std::vector<TimelineRow> threads;          // idle demon first, then by thread number
    std::vector<TimelineMarker> interrupts;    // handler entries (zero time in the model)
    std::uint64_t end = 0;                     // the session's current time
};

// Empty threads for a session without an RTOS; interrupts are shown either way.
Timeline timeline(const Session& session);

}  // namespace latasim::workbench
