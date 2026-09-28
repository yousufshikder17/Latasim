#pragma once
// Host firmware's RTOS: the kernel the CMSIS-RTOS v1 functions (cmsis_rtos1.cpp)
// and latasim_rtos.h act on, bound like the board (binding.hpp).
//
//   mcb1700::Board board;
//   lpc17xx::RtosPort port(board.mcu());
//   rtos::Kernel kernel(port);
//   host::FirmwareBinding bind(board);
//   host::RtosBinding bind_rtos(kernel);   // osThreadCreate, osDelay ... reach `kernel`
//   kernel.start_main(firmware_main);
//   kernel.run_for(...);
//
// While bound, a register store from a thread is a preemption point, and a firmware
// fault inside a thread (an unmapped register, an unsupported RTOS call) stops that
// thread and is thrown from run_until instead of aborting the process.
#include <memory>
#include <string>

#include "rtos/kernel.hpp"

namespace latasim::host {

class RtosBinding {
public:
    explicit RtosBinding(rtos::Kernel& kernel);
    ~RtosBinding();
    RtosBinding(const RtosBinding&) = delete;
    RtosBinding& operator=(const RtosBinding&) = delete;
};

rtos::Kernel* bound_kernel() noexcept;
// Per-binding storage for the C API's object records; cleared when unbound.
std::shared_ptr<void>& bound_rtos_state();
rtos::Kernel& require_bound_kernel(const char* caller);

// For code called from C: report `what` as a fault of the calling thread (see above),
// or abort when not on an RTOS thread.
[[noreturn]] void rtos_fault(const char* caller, const std::string& what);

}  // namespace latasim::host
