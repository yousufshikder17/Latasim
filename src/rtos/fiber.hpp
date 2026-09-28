#pragma once
// A stackful coroutine: code that can stop anywhere in its call stack (for example
// inside unchanged C firmware calling osDelay) and later continue from there.
//
// The RTOS kernel runs each firmware thread on one. A fiber runs only when
// resume() is called and gives control back only by calling suspend() from inside
// itself, so which code runs next is always the kernel's decision, never the host
// operating system's. There is no preemption and no parallelism.
//
// Windows implementation: Win32 fibers (CreateFiber / SwitchToFiber). The calling
// host thread is converted to a fiber on first use. Other platforms would need
// their own implementation of this class (ucontext, a coroutine library).
#include <cstddef>
#include <functional>

namespace latasim::rtos {

class Fiber {
public:
    // `body` must not let an exception escape: it cannot unwind into the resumer.
    Fiber(std::function<void()> body, std::size_t stack_bytes);
    ~Fiber();  // a fiber that never finished is discarded; its stack frames are not unwound
    Fiber(const Fiber&) = delete;
    Fiber& operator=(const Fiber&) = delete;

    // Runs the fiber until it calls suspend() or its body returns. Not from inside
    // the same fiber.
    void resume();
    bool finished() const { return finished_; }

    // From inside a fiber: back to whoever resumed it. Returns when resumed again.
    static void suspend();
    // The fiber running now, or nullptr on the host thread's own stack.
    static Fiber* current();

private:
    static void entry(void* self);

    std::function<void()> body_;
    void* handle_ = nullptr;
    void* caller_ = nullptr;  // the host fiber that resumed this one
    Fiber* outer_ = nullptr;  // current() before resume()
    bool finished_ = false;
};

}  // namespace latasim::rtos
