#include "lpc17xx/ssp.hpp"

namespace latasim::lpc17xx {
namespace {

// CR1 bits (UM10360 Table 371).
constexpr std::uint32_t kLbm = 1u << 0;
constexpr std::uint32_t kSse = 1u << 1;
constexpr std::uint32_t kMs = 1u << 2;

}  // namespace

std::uint32_t Ssp::peek(std::uint32_t offset) const {
    switch (static_cast<SspReg>(offset)) {
    case SspReg::Cr0: return cr0_;
    case SspReg::Cr1: return cr1_;
    case SspReg::Dr: return rx_.empty() ? 0u : rx_.front();
    case SspReg::Sr:
        return kSspTfe | kSspTnf | (rx_.empty() ? 0u : kSspRne) | (rx_.size() == kSspFifoFrames ? kSspRff : 0u);
    case SspReg::Cpsr: return cpsr_;
    }
    return 0;
}

void Ssp::read_side_effects(std::uint32_t offset) {
    if (static_cast<SspReg>(offset) == SspReg::Dr && !rx_.empty()) rx_.pop_front();
}

const char* Ssp::cannot_send() const {
    if (!(cr1_ & kSse)) return "SSP DR write while disabled (CR1.SSE = 0)";
    if (cr1_ & kMs) return "SSP slave mode";
    if (cr1_ & kLbm) return "SSP loopback mode";
    if ((cr0_ & 0x3Fu) != 0x07u) return "SSP frames other than 8-bit SPI (CR0 DSS, FRF)";
    if (rx_.size() == kSspFifoFrames) return "SSP receive overrun (DR written with the receive FIFO full)";
    return nullptr;
}

void Ssp::write(std::uint32_t offset, std::uint32_t value) {
    switch (static_cast<SspReg>(offset)) {
    case SspReg::Cr0: cr0_ = value & 0xFFFFu; break;
    case SspReg::Cr1: cr1_ = value & 0x0Fu; break;
    case SspReg::Cpsr: cpsr_ = value & 0xFEu; break;
    case SspReg::Dr: {
        const auto frame = static_cast<std::uint8_t>(value);
        rx_.push_back(peer_ ? peer_(frame) : std::uint8_t{0xFF});
        break;
    }
    case SspReg::Sr: break;  // read-only: Lpc1768 faults first
    }
}

}  // namespace latasim::lpc17xx
