#pragma once
// A workbench session: one board running one built-in firmware scenario, driven
// in virtual time (docs/phase5/desktop.md). It is what the desktop controls and
// observes, and it has no user interface itself: tests use it the same way.
//
// The simulation stays authoritative: a session owns the board, the RTOS kernel
// when the scenario has one, and the virtual PC on the USB port; callers read their
// state through the model's own inspection functions and change it only through
// board inputs (press, release, potentiometer) and the run controls.
//
// Firmware faults (an unmapped register, an unsupported RTOS call, an interrupt
// storm) stop the session with a message instead of ending the process: each run
// executes on a fiber that a fault abandons (host::fail's escape). A faulted
// session does nothing further; start a new one.
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "boards/mcb1700/board.hpp"
#include "devices/usb_audio_host.hpp"
#include "rtos/kernel.hpp"

namespace latasim::workbench {

class Session;

struct Check {
    bool passed = false;
    std::string detail;
};

struct Scenario {
    std::string name;
    std::string description;
    bool rtos = false;
    rtos::Config config;
    bool usb_pc = false;  // a virtual PC plugged into the USB port, playing a tone
    std::function<void()> reset_statics;        // before the board exists
    std::function<void(Session&)> start;        // bare metal: bind handlers, start firmware
    // Bare metal with a main loop: the firmware's main(), on a fiber of its own. It
    // runs whenever virtual time has caught up with the processor time it consumed
    // (latasim_consume_cycles), so each register access it makes happens at the
    // right virtual time; a busy loop that consumes nothing never lets time pass.
    // Time consumed outside it (in interrupt handlers) is not counted: handlers
    // take no virtual time. Returning from it leaves the processor idle.
    std::function<void()> bare_main;
    void (*rtos_main)() = nullptr;              // RTOS: the firmware's main()
    std::function<void(Session&)> teardown;     // while still bound
    std::uint64_t check_after = 0;              // cycles after which check() is meaningful
    std::function<Check(const Session&)> check;
    // A line of firmware-level state for the desktop (buffer levels, counters).
    std::function<std::string(const Session&)> status;
};

// The built-in scenarios (scenarios.cpp).
const std::vector<Scenario>& scenarios();

class Session {
public:
    explicit Session(std::size_t scenario);  // a built-in one
    explicit Session(Scenario scenario);
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    const Scenario& scenario() const { return scenario_; }
    mcb1700::Board& board();
    const mcb1700::Board& board() const;
    rtos::Kernel* kernel() { return kernel_.get(); }
    const rtos::Kernel* kernel() const { return kernel_.get(); }
    usb::AudioHost* pc() { return pc_.get(); }
    const usb::AudioHost* pc() const { return pc_.get(); }

    std::uint64_t now() const;
    bool faulted() const { return !fault_.empty(); }
    const std::string& fault() const { return fault_; }

    void run_for(std::uint64_t cycles);
    void run_until(std::uint64_t cycle);
    // Advance to the machine's next scheduled event (a tick, a match, a conversion,
    // a USB frame), or 1 ms if nothing is scheduled.
    void step();

    // The scenario's pass/fail check, if it has one and has run long enough.
    Check check() const;
    std::string status() const { return scenario_.status ? scenario_.status(*this) : std::string{}; }

    // Board inputs, guarded like runs (an input can run interrupt handlers).
    void input(const std::function<void(mcb1700::Board&)>& change);

private:
    struct Parts;
    void guarded(const std::function<void()>& work);
    void run_main_until(std::uint64_t cycle);

    Scenario scenario_;
    std::unique_ptr<Parts> parts_;
    std::unique_ptr<rtos::Kernel> kernel_;
    std::unique_ptr<usb::AudioHost> pc_;
    std::unique_ptr<usb::TonePcm> tone_;
    std::string fault_;
};

}  // namespace latasim::workbench
