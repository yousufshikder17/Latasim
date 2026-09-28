#include "lpc17xx/adc.hpp"

#include <stdexcept>
#include <string>

namespace latasim::lpc17xx {
namespace {

constexpr std::uint32_t kBurst = 1u << 16;
constexpr std::uint32_t kPdn = 1u << 21;
constexpr std::uint32_t kGlobalInterrupt = 1u << 8;  // ADGINTEN
constexpr std::uint32_t kDone = 1u << 31;

std::uint32_t start_field(std::uint32_t adcr) { return (adcr >> 24) & 7u; }

}  // namespace

bool Adc::modelled(std::uint32_t offset) {
    return offset == 0x00 || offset == 0x04 || offset == 0x0C || (offset >= 0x10 && offset <= 0x30 && offset % 4 == 0);
}

bool Adc::unsupported_control(std::uint32_t adcr) { return (adcr & kBurst) || start_field(adcr) > 1; }

std::uint32_t Adc::peek(std::uint32_t offset) const {
    if (offset == 0x00) return adcr_;
    if (offset == 0x04) return (global_done_ ? kDone : 0u) | (global_channel_ << 24) | (global_result_ << 4);
    if (offset == 0x0C) return adinten_;
    if (offset == 0x30) {
        std::uint32_t value = interrupt() ? 1u << 16 : 0u;  // ADINT
        for (unsigned n = 0; n < 8; ++n) value |= done_[n] ? 1u << n : 0u;
        return value;
    }
    const unsigned n = (offset - 0x10) / 4;  // ADDRn
    return (done_[n] ? kDone : 0u) | (results_[n] << 4);
}

void Adc::read_side_effects(std::uint32_t offset) {
    if (offset == 0x04) global_done_ = false;
    else if (offset >= 0x10 && offset <= 0x2C) done_[(offset - 0x10) / 4] = false;
}

void Adc::write(std::uint32_t offset, std::uint32_t value, std::uint64_t conversion_cycles) {
    if (offset == 0x0C) {
        adinten_ = value & 0x1FFu;
        return;
    }
    if (offset != 0x00) return;  // ADGDR, ADDRn, ADSTAT: data and status, writes ignored
    adcr_ = value;
    global_done_ = false;  // "cleared ... when the ADCR is written"
    if (start_field(value) != 1 || !(value & kPdn)) return;
    unsigned channel = 0;
    const std::uint32_t sel = value & 0xFFu;
    while (sel && !(sel & (1u << channel))) ++channel;
    busy_ = true;
    remaining_ = conversion_cycles;
    converting_ = channel;
    sample_ = inputs_[channel];
}

void Adc::set_input(unsigned channel, std::uint32_t raw) {
    if (raw > kAdcMaxInput) throw std::out_of_range("ADC input " + std::to_string(raw) + " above 0xFFF");
    inputs_.at(channel) = raw;
}

void Adc::advance(std::uint64_t cycles) {
    if (!busy_) return;
    if (cycles < remaining_) {
        remaining_ -= cycles;
        return;
    }
    busy_ = false;
    remaining_ = 0;
    global_result_ = sample_;
    global_channel_ = converting_;
    global_done_ = true;
    results_[converting_] = sample_;
    done_[converting_] = true;
    completed_ = true;
}

bool Adc::interrupt() const {
    if (adinten_ & kGlobalInterrupt) return global_done_;
    for (unsigned n = 0; n < 8; ++n)
        if (done_[n] && (adinten_ & (1u << n))) return true;
    return false;
}

bool Adc::take_completed(unsigned& channel, std::uint32_t& result) {
    if (!completed_) return false;
    completed_ = false;
    channel = global_channel_;
    result = global_result_;
    return true;
}

}  // namespace latasim::lpc17xx
