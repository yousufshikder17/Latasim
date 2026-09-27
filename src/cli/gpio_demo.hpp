#pragma once
#include <ostream>

namespace latasim::cli {

// `vwb gpio-demo`: drives MCB1700 LEDs through direct register stores and
// through the Keil board API, printing the LED and FIOnPIN state after each step.
// Deterministic: the same output on every run.
void run_gpio_demo(std::ostream& out);

}  // namespace latasim::cli
