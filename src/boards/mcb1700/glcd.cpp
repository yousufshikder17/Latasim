#include "boards/mcb1700/glcd.hpp"

namespace latasim::mcb1700 {
namespace {

constexpr std::uint8_t kStartMask = 0xFC;
constexpr std::uint8_t kStart = 0x70;
constexpr std::uint8_t kRs = 0x02;
constexpr std::uint8_t kRw = 0x01;

constexpr std::uint8_t kEntryMode = 0x03;
constexpr std::uint8_t kGramH = 0x20, kGramV = 0x21, kGramData = 0x22;
constexpr std::uint8_t kHStart = 0x50, kHEnd = 0x51, kVStart = 0x52, kVEnd = 0x53;

}  // namespace

Glcd::Glcd() {
    regs_[kHEnd] = kGramWidth - 1;
    regs_[kVEnd] = kGramHeight - 1;
}

void Glcd::chip_select(bool selected) {
    if (selected == selected_) return;
    selected_ = selected;
    if (burst_ != 0) writes_.push_back({kGramData, burst_});
    burst_ = 0;
    phase_ = selected ? Phase::Start : Phase::Idle;
    high_byte_ = true;
}

std::uint8_t Glcd::shift(std::uint8_t mosi) {
    if (!selected_) return 0xFF;
    switch (phase_) {
    case Phase::Start:
        if ((mosi & kStartMask) != kStart) {
            ++bad_start_bytes_;
            phase_ = Phase::Ignore;
            return 0;
        }
        if (mosi & kRw) {
            phase_ = (mosi & kRs) ? Phase::Read : Phase::Ignore;  // status reads: not modelled
            dummy_ = true;
            high_byte_ = true;
        } else {
            phase_ = (mosi & kRs) ? Phase::Data : Phase::Index;
        }
        return 0;
    case Phase::Index:
    case Phase::Data:
        if (high_byte_) {
            word_ = static_cast<std::uint16_t>(mosi << 8);
            high_byte_ = false;
            return 0;
        }
        word_ = static_cast<std::uint16_t>(word_ | mosi);
        high_byte_ = true;
        if (phase_ == Phase::Index) index_ = static_cast<std::uint8_t>(word_);
        else write_data(word_);
        return 0;
    case Phase::Read: {
        if (dummy_) {
            dummy_ = false;
            return 0;
        }
        const std::uint16_t value = index_ == 0x00 ? kId : regs_[index_];
        const auto out = static_cast<std::uint8_t>(high_byte_ ? value >> 8 : value & 0xFF);
        high_byte_ = !high_byte_;
        return out;
    }
    case Phase::Idle:
    case Phase::Ignore: break;
    }
    return 0;
}

void Glcd::write_data(std::uint16_t value) {
    if (index_ == kGramData) {
        gram_[v_ * kGramWidth + h_] = value;
        ++burst_;
        advance_address();
        return;
    }
    regs_[index_] = value;
    writes_.push_back({index_, value});
    if (index_ == kGramH) h_ = value % kGramWidth;
    if (index_ == kGramV) v_ = value % kGramHeight;
}

// After a GRAM write the counter moves along one axis (AM: vertical first) and
// wraps inside the window onto the other, in the directions I/D selects.
void Glcd::advance_address() {
    const std::uint16_t entry = regs_[kEntryMode];
    const bool h_up = entry & 0x10, v_up = entry & 0x20, vertical_first = entry & 0x08;
    const unsigned hs = regs_[kHStart] % kGramWidth, he = regs_[kHEnd] % kGramWidth;
    const unsigned vs = regs_[kVStart] % kGramHeight, ve = regs_[kVEnd] % kGramHeight;
    auto step = [](unsigned& a, bool up, unsigned lo, unsigned hi) {  // true when it wrapped
        if (up ? a >= hi : a <= lo) {
            a = up ? lo : hi;
            return true;
        }
        a = up ? a + 1 : a - 1;
        return false;
    };
    if (vertical_first) {
        if (step(v_, v_up, vs, ve)) step(h_, h_up, hs, he);
    } else {
        if (step(h_, h_up, hs, he)) step(v_, v_up, vs, ve);
    }
}

std::uint64_t Glcd::hash() const {
    std::uint64_t h = 1469598103934665603ull;
    for (const std::uint16_t p : gram_) {
        h = (h ^ (p & 0xFF)) * 1099511628211ull;
        h = (h ^ (p >> 8)) * 1099511628211ull;
    }
    return h;
}

std::vector<Glcd::Write> Glcd::take_writes() {
    std::vector<Write> out;
    out.swap(writes_);
    return out;
}

}  // namespace latasim::mcb1700
