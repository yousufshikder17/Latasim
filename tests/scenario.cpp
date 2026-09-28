#include "scenario.hpp"

#include <cstdio>
#include <stdexcept>
#include <string>

namespace latasim::test {
namespace {

constexpr std::size_t kRecentEvents = 6;

std::string hex(std::uint32_t value) {
    char text[16];
    std::snprintf(text, sizeof text, "0x%08X", static_cast<unsigned>(value));
    return text;
}

}  // namespace

Scenario::Scenario() = default;

void Scenario::on_systick(std::function<void()> handler) { board_.mcu().on_systick(std::move(handler)); }

void Scenario::run_for(Cycles duration) { board_.mcu().advance_cycles(duration.count()); }

void Scenario::run_until(Cycles time) {
    if (time < now())
        throw std::logic_error("run_until(" + std::to_string(time.count()) + " cycles): virtual time is already " +
                               std::to_string(now().count()) + " cycles");
    run_for(time - now());
}

testing::AssertionResult Scenario::led(unsigned index, mcb1700::LedState expected) const {
    const mcb1700::LedState actual = board_.led(index);
    if (actual == expected) return testing::AssertionSuccess();
    return fail("LED" + std::to_string(index) + " is " + to_string(actual) + ", expected " + to_string(expected));
}

testing::AssertionResult Scenario::pin(unsigned port, unsigned pin, Level expected) const {
    const bool high = board_.mcu().gpio().pin_level(port, pin);
    if (high == (expected == Level::High)) return testing::AssertionSuccess();
    return fail("P" + std::to_string(port) + "." + std::to_string(pin) + " is " + (high ? "high" : "low") +
                ", expected " + (expected == Level::High ? "high" : "low"));
}

testing::AssertionResult Scenario::reg(std::uint32_t address, std::uint32_t expected) const {
    const std::uint32_t actual = board_.mcu().peek32(address);
    if (actual == expected) return testing::AssertionSuccess();
    return fail("register " + hex(address) + " is " + hex(actual) + ", expected " + hex(expected));
}

// "<what>\n  at t = <cycles> cycles (<ms> ms)\n  last trace events:\n    #..."
testing::AssertionResult Scenario::fail(const std::string& what) const {
    const std::uint64_t cycles = now().count();
    char time[96];
    std::snprintf(time, sizeof time, "\n  at t = %llu cycles (%.6f ms)", static_cast<unsigned long long>(cycles),
                  static_cast<double>(cycles) / static_cast<double>(lpc17xx::kCyclesPerMillisecond));
    testing::AssertionResult result = testing::AssertionFailure() << what << time;
    const auto& events = board_.mcu().trace().events();
    if (events.empty()) return result << "\n  (trace is empty)";
    result << "\n  last trace events:";
    const std::size_t first = events.size() > kRecentEvents ? events.size() - kRecentEvents : 0;
    for (std::size_t i = first; i < events.size(); ++i) result << "\n    " << to_string(events[i]);
    return result;
}

}  // namespace latasim::test
