#pragma once
// The host-firmware bridge.
//
// Firmware C APIs such as Keil's GPIO_SetDir(port, pin, dir) take no context
// argument, so host-compiled firmware can only reach "the" board. This is that
// one piece of global state: a scoped binding of exactly one board, owned by C++.
// It exists for host-compiled firmware only; the model itself has no globals.
//
//   mcb1700::Board board;
//   host::FirmwareBinding bind(board);   // firmware calls now reach `board`
//   LED_On(0);                           // (C firmware)
//   // leaving the scope unbinds
//
// Misuse is loud: a second concurrent binding throws, and a firmware call with no
// board bound, or with an invalid pin, aborts with a message (an exception cannot
// safely unwind through C frames).
#include <cstdint>
#include <functional>
#include <string>

#include "boards/mcb1700/board.hpp"

namespace latasim::host {

class FirmwareBinding {
public:
    explicit FirmwareBinding(mcb1700::Board& board);
    ~FirmwareBinding();
    FirmwareBinding(const FirmwareBinding&) = delete;
    FirmwareBinding& operator=(const FirmwareBinding&) = delete;
};

// The bound board, or nullptr.
mcb1700::Board* bound_board() noexcept;

// For code called from C: the bound board, or abort with a message naming `caller`.
mcb1700::Board& require_bound_board(const char* caller);

// Print "latasim host: <caller>: <what>" to stderr and abort, unless a fault
// escape is installed and does not return (an RTOS thread abandons its fiber and
// the error reaches the caller of run_until, see host/rtos_binding.hpp).
[[noreturn]] void fail(const char* caller, const char* what);
void set_fault_escape(std::function<void(const std::string& message)> escape);

// Called after every register store from host firmware: where an RTOS may preempt
// the storing thread if the store's interrupt readied a higher-priority one.
void set_store_hook(std::function<void()> hook);
void after_store();

// Processor time host firmware consumes (latasim_consume_cycles, latasim_rtos.h)
// outside an RTOS: while a hook is installed it takes the cycles instead of the RTOS
// kernel. The workbench installs one for a bare-metal main() (workbench/session.hpp).
void set_consume_hook(std::function<void(std::uint64_t cycles)> hook);
bool consume(std::uint64_t cycles);  // false: no hook installed
// Called at the start of every firmware access to the board (require_bound_board),
// so that time the firmware consumed can be settled first.
void set_access_hook(std::function<void()> hook);

}  // namespace latasim::host
