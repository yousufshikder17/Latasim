#pragma once
#include <ostream>

namespace latasim::cli {

// `latasim firmware-demo`: Keil's MCB1700 sources (unmodified C, from the installed
// packs) on modelled boards. Part 1 runs the LED, joystick and button drivers with
// injected inputs; part 2 runs Blinky_ULp's SysTick_Handler over 25 ms of virtual
// time. Prints each step with the trace events it caused. Deterministic: the same
// output on every run in a new process. Built only when the Keil packs are present.
void run_firmware_demo(std::ostream& out);

}  // namespace latasim::cli
