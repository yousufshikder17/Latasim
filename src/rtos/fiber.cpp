#include "rtos/fiber.hpp"

#include <stdexcept>

#ifndef _WIN32
#error "latasim::rtos::Fiber has only a Win32 implementation"
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace latasim::rtos {
namespace {

thread_local Fiber* current_fiber = nullptr;

// The host thread's own fiber, converting the thread on first use.
void* host_fiber() {
    if (IsThreadAFiber()) return GetCurrentFiber();
    void* fiber = ConvertThreadToFiber(nullptr);
    if (fiber == nullptr) throw std::runtime_error("ConvertThreadToFiber failed");
    return fiber;
}

}  // namespace

Fiber::Fiber(std::function<void()> body, std::size_t stack_bytes) : body_(std::move(body)) {
    handle_ = CreateFiber(stack_bytes, reinterpret_cast<LPFIBER_START_ROUTINE>(&Fiber::entry), this);
    if (handle_ == nullptr) throw std::runtime_error("CreateFiber failed");
}

Fiber::~Fiber() {
    if (handle_ != nullptr) DeleteFiber(handle_);
}

void Fiber::entry(void* self) {
    auto* fiber = static_cast<Fiber*>(self);
    fiber->body_();
    fiber->finished_ = true;
    SwitchToFiber(fiber->caller_);  // never resumed again
}

void Fiber::resume() {
    if (finished_) throw std::logic_error("resuming a finished fiber");
    if (current_fiber == this) throw std::logic_error("a fiber cannot resume itself");
    caller_ = current_fiber != nullptr ? current_fiber->handle_ : host_fiber();
    outer_ = current_fiber;
    current_fiber = this;
    SwitchToFiber(handle_);
    current_fiber = outer_;
}

void Fiber::suspend() {
    Fiber* self = current_fiber;
    if (self == nullptr) throw std::logic_error("suspend() outside a fiber");
    SwitchToFiber(self->caller_);
}

Fiber* Fiber::current() { return current_fiber; }

}  // namespace latasim::rtos
