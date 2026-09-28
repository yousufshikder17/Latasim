#include "workbench/session.hpp"

#include <stdexcept>

#include "host/binding.hpp"
#include "host/rtos_binding.hpp"
#include "lpc17xx/rtos_port.hpp"
#include "rtos/fiber.hpp"

namespace latasim::workbench {
namespace {

// A bare-metal main() yields to let virtual time catch up at its next access to the
// board, or once it has consumed this much (about 1 ms) without one.
constexpr std::uint64_t kMainSettleCycles = lpc17xx::kCyclesPerMillisecond;

}  // namespace

struct Session::Parts {
    mcb1700::Board board;
    host::FirmwareBinding bind{board};
    lpc17xx::RtosPort port{board.mcu()};
    std::unique_ptr<host::RtosBinding> bind_rtos;
    rtos::Fiber* active = nullptr;  // the fiber of the run in progress
    bool tearing_down = false;
    std::unique_ptr<rtos::Fiber> main;  // Scenario::bare_main's
    std::uint64_t main_owed = 0;        // cycles main used that virtual time has yet to catch up with
    std::uint64_t main_unsettled = 0;   // cycles main consumed since it last yielded

    bool on_main() const { return main && rtos::Fiber::current() == main.get(); }
    void settle_main() {
        main_owed += main_unsettled;
        main_unsettled = 0;
        rtos::Fiber::suspend();
    }
};

Session::Session(std::size_t index) : Session(scenarios().at(index)) {}

Session::Session(Scenario scenario) : scenario_(std::move(scenario)) {
    if (scenario_.reset_statics) scenario_.reset_statics();
    parts_ = std::make_unique<Parts>();
    if (scenario_.rtos) {
        kernel_ = std::make_unique<rtos::Kernel>(parts_->port, scenario_.config);
        parts_->bind_rtos = std::make_unique<host::RtosBinding>(*kernel_);
    }
    if (scenario_.bare_main) {
        parts_->main = std::make_unique<rtos::Fiber>(
            [this] {
                try {
                    scenario_.bare_main();
                } catch (const std::exception& e) {
                    fault_ = e.what();
                } catch (...) {
                    fault_ = "unknown error";
                }
            },
            1 << 20);
        host::set_consume_hook([this](std::uint64_t cycles) {
            Parts& p = *parts_;
            if (!p.on_main()) return;  // an interrupt handler: no virtual time
            p.main_unsettled += cycles;
            if (p.main_unsettled >= kMainSettleCycles) p.settle_main();
        });
        host::set_access_hook([this] {
            if (parts_->on_main() && parts_->main_unsettled != 0) parts_->settle_main();
        });
    }
    // After RtosBinding, which installs its own: a fault in an RTOS thread still
    // abandons that thread; one in a bare-metal main abandons main; one anywhere
    // else in a run abandons the run.
    host::set_fault_escape([this](const std::string& message) {
        if (kernel_ && kernel_->in_thread()) kernel_->abandon_thread(std::make_exception_ptr(std::runtime_error(message)));
        if (parts_->on_main()) {
            fault_ = message;
            rtos::Fiber::suspend();  // never resumed
        }
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
        if (parts_->main) run_main_until(now());  // what main does before it first uses time
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
    host::set_consume_hook(nullptr);
    host::set_access_hook(nullptr);
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
        else if (parts_->main) run_main_until(cycle);
        else if (cycle > now()) board().mcu().advance_cycles(cycle - now());
    });
}

// Main runs while it owes no time; otherwise time advances by what it owes (taking
// interrupts on the way), and at most to `cycle`.
void Session::run_main_until(std::uint64_t cycle) {
    Parts& p = *parts_;
    for (;;) {
        const bool running = !p.main->finished() && !faulted();
        if (running && p.main_owed == 0) {
            p.main->resume();
            continue;
        }
        if (faulted() || now() >= cycle) return;
        std::uint64_t step = cycle - now();
        if (running && p.main_owed < step) step = p.main_owed;
        board().mcu().advance_cycles(step);
        if (running) p.main_owed -= step;
    }
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
