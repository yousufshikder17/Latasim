#include "host/rtos_binding.hpp"

#include <stdexcept>

#include "host/binding.hpp"
#include "latasim_rtos.h"

namespace latasim::host {
namespace {

rtos::Kernel* g_kernel = nullptr;
std::shared_ptr<void> g_state;

}  // namespace

std::shared_ptr<void>& bound_rtos_state() { return g_state; }

RtosBinding::RtosBinding(rtos::Kernel& kernel) {
    if (g_kernel != nullptr) throw std::logic_error("latasim host: an RTOS kernel is already bound");
    g_kernel = &kernel;
    set_store_hook([&kernel] { kernel.preemption_point(); });
    set_fault_escape([&kernel](const std::string& message) {
        if (kernel.in_thread()) kernel.abandon_thread(std::make_exception_ptr(std::runtime_error(message)));
    });
}

RtosBinding::~RtosBinding() {
    set_store_hook(nullptr);
    set_fault_escape(nullptr);
    g_state.reset();
    g_kernel = nullptr;
}

rtos::Kernel* bound_kernel() noexcept { return g_kernel; }

rtos::Kernel& require_bound_kernel(const char* caller) {
    if (g_kernel == nullptr) fail(caller, "no RTOS kernel bound (create a latasim::host::RtosBinding first)");
    return *g_kernel;
}

void rtos_fault(const char* caller, const std::string& what) { fail(caller, what.c_str()); }

}  // namespace latasim::host

extern "C" {

void latasim_consume_cycles(uint32_t cycles) {
    if (latasim::host::consume(cycles)) return;  // a bare-metal main() (host/binding.hpp)
    auto& k = latasim::host::require_bound_kernel("latasim_consume_cycles");
    if (!k.in_thread()) latasim::host::fail("latasim_consume_cycles", "not called from an RTOS thread");
    k.consume(cycles);
}

void latasim_consume_us(uint32_t microseconds) { latasim_consume_cycles(microseconds * 100u); }

}
