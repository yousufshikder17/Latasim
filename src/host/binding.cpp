#include "host/binding.hpp"

#include <cstdio>
#include <cstdlib>
#include <stdexcept>

namespace latasim::host {
namespace {

mcb1700::Board* g_bound = nullptr;
std::function<void(const std::string&)> g_fault_escape;
std::function<void()> g_store_hook;
std::function<void(std::uint64_t)> g_consume_hook;
std::function<void()> g_access_hook;

}  // namespace

void set_fault_escape(std::function<void(const std::string&)> escape) { g_fault_escape = std::move(escape); }
void set_store_hook(std::function<void()> hook) { g_store_hook = std::move(hook); }
void set_consume_hook(std::function<void(std::uint64_t)> hook) { g_consume_hook = std::move(hook); }
void set_access_hook(std::function<void()> hook) { g_access_hook = std::move(hook); }

bool consume(std::uint64_t cycles) {
    if (!g_consume_hook) return false;
    g_consume_hook(cycles);
    return true;
}

void after_store() {
    if (g_store_hook) g_store_hook();
}

FirmwareBinding::FirmwareBinding(mcb1700::Board& board) {
    if (g_bound != nullptr) throw std::logic_error("latasim host: a board is already bound to host firmware");
    g_bound = &board;
}

FirmwareBinding::~FirmwareBinding() { g_bound = nullptr; }

mcb1700::Board* bound_board() noexcept { return g_bound; }

mcb1700::Board& require_bound_board(const char* caller) {
    if (g_bound == nullptr) fail(caller, "no board bound (create a latasim::host::FirmwareBinding first)");
    if (g_access_hook) g_access_hook();
    return *g_bound;
}

void fail(const char* caller, const char* what) {
    if (g_fault_escape) g_fault_escape(std::string(caller) + ": " + what);  // returns only if it cannot escape
    std::fprintf(stderr, "latasim host: %s: %s\n", caller, what);
    std::fflush(stderr);
    std::abort();
}

}  // namespace latasim::host
