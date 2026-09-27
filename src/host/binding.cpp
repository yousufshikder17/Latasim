#include "host/binding.hpp"

#include <cstdio>
#include <cstdlib>
#include <stdexcept>

namespace latasim::host {
namespace {

mcb1700::Board* g_bound = nullptr;

}  // namespace

FirmwareBinding::FirmwareBinding(mcb1700::Board& board) {
    if (g_bound != nullptr) throw std::logic_error("latasim host: a board is already bound to host firmware");
    g_bound = &board;
}

FirmwareBinding::~FirmwareBinding() { g_bound = nullptr; }

mcb1700::Board* bound_board() noexcept { return g_bound; }

mcb1700::Board& require_bound_board(const char* caller) {
    if (g_bound == nullptr) fail(caller, "no board bound (create a latasim::host::FirmwareBinding first)");
    return *g_bound;
}

void fail(const char* caller, const char* what) {
    std::fprintf(stderr, "latasim host: %s: %s\n", caller, what);
    std::fflush(stderr);
    std::abort();
}

}  // namespace latasim::host
