#pragma once
#include <ostream>

namespace latasim::cli {

// `latasim firmware-demo`: Keil's MCB1700 LED, joystick and button drivers
// (unmodified C, from the installed packs) run on a modelled board. Prints each
// firmware call and board input with the trace events it caused. Deterministic:
// the same output on every run. Built only when the Keil packs are present.
void run_firmware_demo(std::ostream& out);

}  // namespace latasim::cli
