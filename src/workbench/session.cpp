#include "workbench/session.hpp"

#include <stdexcept>

#include "host/binding.hpp"
#include "host/rtos_binding.hpp"
#include "lpc17xx/rtos_port.hpp"
#include "rtos/fiber.hpp"

namespace latasim::workbench {

struct Session::Parts {
    mcb1700::Board board;
    host::FirmwareBinding bind{board};
    lpc17xx::RtosPort port{board.mcu()};
    std::unique_ptr<host::RtosBinding> bind_rtos;
    rtos::Fiber* active = nullptr;  // the fiber of the run in progress
    bool tearing_down = false;
};

Session::Session(std::size_t index) : scenario_(scenarios().at(index)) {
    if (scenario_.reset_statics) scenario_.reset_statics();
    parts_ = std::make_unique<Parts>();
    if (scenario_.rtos) {
        kernel_ = std::make_unique<rtos::Kernel>(parts_->port, scenario_.config);
        parts_->bind_rtos = std::make_unique<host::RtosBinding>(*kernel_);
    }
    // After RtosBinding, which installs its own: a fault in an RTOS thread still
    // abandons that thread; one anywhere else in a run abandons the run.
    host::set_fault_escape([this](const std::string& message) {
        if (kernel_ && kernel_->in_thread()) kernel_->abandon_thread(std::make_exception_ptr(std::runtime_error(message)));
        if (parts_->active != nullptr && rtos::Fiber::current() == parts_->active) {
            fault_ = message;
            rtos::Fiber::suspend();  // never resumed
        }
    });
    if (scenario_.usb_pc) {
        tone_ = std::make_unique<usb::TonePcm>(32'000u, 440u, std::int16_t{6'000});
        pc_ = std::make_unique<usb::AudioHost>();
        pc_->set_source(tone_.get());
    }
    guarded([this] {
        if (scenario_.start) scenario_.start(*this);
        if (scenario_.rtos && scenario_.rtos_main != nullptr) kernel_->start_main(scenario_.rtos_main);
        if (pc_) board().connect_usb_host(pc_.get());
    });
}

Session::~Session() {
    // Firmware teardown while everything is bound (Keil's USB driver keeps its state
    // in statics that outlive the board), then no hook may reach the kernel again.
    parts_->tearing_down = true;
    if (scenario_.teardown) guarded([this] { scenario_.teardown(*this); });
    auto& mcu = board().mcu();
    mcu.attach_usb_host(nullptr);
    mcu.on_thread_mode({});
    parts_->bind_rtos.reset();
    host::set_fault_escape(nullptr);
    kernel_.reset();
    parts_.reset();
}

mcb1700::Board& Session::board() { return parts_->board; }
const mcb1700::Board& Session::board() const { return parts_->board; }

std::uint64_t Session::now() const { return parts_->board.mcu().cycles(); }

void Session::guarded(const std::function<void()>& work) {
    if (faulted() && !parts_->tearing_down) return;
    std::exception_ptr error;
    rtos::Fiber fiber(
        [&] {
            try {
                work();
            } catch (...) {
                error = std::current_exception();
            }
        },
        1 << 20);
    parts_->active = &fiber;
    fiber.resume();
    parts_->active = nullptr;
    if (error) {
        try {
            std::rethrow_exception(error);
        } catch (const std::exception& e) {
            fault_ = e.what();
        } catch (...) {
            fault_ = "unknown error";
        }
    } else if (!fiber.finished() && fault_.empty()) {
        fault_ = "firmware stopped";
    }
}

void Session::run_for(std::uint64_t cycles) { run_until(now() + cycles); }

void Session::run_until(std::uint64_t cycle) {
    guarded([&] {
        if (kernel_) kernel_->run_until(cycle);
        else if (cycle > now()) board().mcu().advance_cycles(cycle - now());
    });
}

void Session::step() {
    const std::uint64_t next = board().mcu().cycles_to_next_event();
    run_for(next != 0 ? next : lpc17xx::kCyclesPerMillisecond);
}

Check Session::check() const {
    if (!scenario_.check) return {false, "this scenario has no check"};
    if (faulted()) return {false, "faulted: " + fault_};
    if (now() < scenario_.check_after)
        return {false, "run to t = " + std::to_string(scenario_.check_after / lpc17xx::kCyclesPerMillisecond) + " ms first"};
    return scenario_.check(*this);
}

void Session::input(const std::function<void(mcb1700::Board&)>& change) {
    guarded([&] { change(board()); });
}

}  // namespace latasim::workbench
