#include "lpc17xx/lpc1768.hpp"

#include <cstdio>
#include <stdexcept>
#include <string>

namespace latasim::lpc17xx {
namespace {

std::string fault_message(std::uint32_t address) {
    char text[64];
    std::snprintf(text, sizeof text, "bus fault: no register at 0x%08X", static_cast<unsigned>(address));
    return text;
}

bool is_systick(std::uint32_t address) { return address - kSysTickBase < 0x10; }

// SysTick registers are word accesses here; narrow access is not modeled.
SysTickReg systick_reg(std::uint32_t address, unsigned size) {
    if (size != 4 || (address & 3u) != 0) throw BusFault(address);
    return static_cast<SysTickReg>(address - kSysTickBase);
}

// Timer n's register offset for an address in its window, or -1.
int timer_at(std::uint32_t address, std::uint32_t& offset) {
    for (unsigned n = 0; n < kTimerBase.size(); ++n) {
        offset = address - kTimerBase[n];
        if (offset < kTimerWindow) return static_cast<int>(n);
    }
    return -1;
}

bool is_adc(std::uint32_t address) { return address - kAdcBase < 0x38; }

std::uint32_t adc_offset(std::uint32_t address, unsigned size) {
    const std::uint32_t offset = address - kAdcBase;
    if (size != 4 || !Adc::modelled(offset)) throw BusFault(address);
    return offset;
}

TimerReg timer_reg(std::uint32_t address, std::uint32_t offset, unsigned size) {
    if (size != 4 || !Timer::modelled(offset)) throw BusFault(address);
    return static_cast<TimerReg>(offset);
}

bool is_alias(std::uint32_t address) {
    return address >= kBitBandAliasBase && address - kBitBandAliasBase < kBitBandSize * 32;
}

struct AliasTarget {
    std::uint32_t word_address;
    unsigned bit;
};

AliasTarget alias_target(std::uint32_t alias) {
    const std::uint32_t offset = alias - kBitBandAliasBase;
    const std::uint32_t byte_address = kBitBandBase + offset / 32;
    const unsigned bit_in_byte = (offset % 32) / 4;
    return {byte_address & ~3u, (byte_address & 3u) * 8 + bit_in_byte};
}

}  // namespace

// Reads the word an alias points at; a fault names the alias the firmware used.
std::uint32_t Lpc1768::read_target(std::uint32_t alias, std::uint32_t word_address) const {
    try {
        return load(word_address, 4);
    } catch (const BusFault&) {
        throw BusFault(alias);
    }
}

BusFault::BusFault(std::uint32_t address) : std::runtime_error(fault_message(address)), address_(address) {}

NotModelled::NotModelled(std::uint32_t address, const std::string& what)
    : std::runtime_error([&] {
          char text[96];
          std::snprintf(text, sizeof text, "not modelled: %s (register 0x%08X)", what.c_str(),
                        static_cast<unsigned>(address));
          return std::string(text);
      }()) {}

// Maps a GPIO address and access size (1, 2 or 4 bytes) to a register and the byte
// lanes it covers. Narrow accesses must be naturally aligned inside one register.
Lpc1768::GpioTarget Lpc1768::decode_gpio(std::uint32_t address, unsigned size) {
    if (address < kGpioBase || address % size != 0) throw BusFault(address);
    const std::uint32_t offset = address - kGpioBase;
    const unsigned port = offset / kGpioPortStride;
    if (port >= Gpio::kPortCount) throw BusFault(address);
    const std::uint32_t in_port = offset % kGpioPortStride;
    const auto reg = static_cast<GpioReg>(in_port & ~3u);
    const unsigned shift = (in_port & 3u) * 8;
    const std::uint32_t lanes = size == 4 ? 0xFFFFFFFFu : ((std::uint32_t{1} << (size * 8)) - 1) << shift;
    switch (reg) {
    case GpioReg::Dir:
    case GpioReg::Mask:
    case GpioReg::Pin:
    case GpioReg::Set:
    case GpioReg::Clr:
        return {port, reg, shift, lanes};
    }
    throw BusFault(address);  // reserved offsets 0x04-0x0F
}

// Time moves in steps that end at each event that can change interrupt or
// peripheral run state (a SysTick count to 0 while TICKINT is set, a timer match
// with an MCR action and the edge after it), so exceptions are taken at the
// virtual time they became due. Each step is computed arithmetically.
void Lpc1768::advance_cycles(std::uint64_t cycles) {
    if (in_handler_) throw std::logic_error("advance_cycles called from an interrupt handler");
    while (cycles > 0) {
        const std::uint64_t to_event = cycles_to_next_event();
        const std::uint64_t step = to_event == 0 || to_event > cycles ? cycles : to_event;
        const auto before = pending_snapshot();
        advance_peripherals(step);
        cycles -= step;
        update_interrupt_lines();
        trace_new_pending(before);
        service_interrupts();
    }
}

std::uint64_t Lpc1768::adc_conversion_cycles() const { return adc_conversion_cycles_for(adc_.peek(0)); }

// For an ADCR value: its CLKDIV and PCLK_ADC (PCLKSEL0[25:24]).
std::uint64_t Lpc1768::adc_conversion_cycles_for(std::uint32_t adcr) const {
    static constexpr std::uint32_t kDivider[] = {4, 1, 2, 8};
    const std::uint64_t pclk = kDivider[(pclksel_[0] >> 24) & 3u];
    return kAdcConversionClocks * (((adcr >> 8) & 0xFFu) + 1) * pclk;
}

std::uint32_t Lpc1768::timer_divider(unsigned n) const {
    static constexpr std::uint32_t kDivider[] = {4, 1, 2, 8};
    const std::uint32_t field = n < 2 ? (pclksel_[0] >> (2 + 2 * n)) & 3u : (pclksel_[1] >> (12 + 2 * (n - 2))) & 3u;
    return kDivider[field];
}

// 0 = nothing due. PCLK edges fall on multiples of the divider since reset.
std::uint64_t Lpc1768::cycles_to_next_event() const {
    std::uint64_t next = systick_.interrupt_enabled() ? systick_.cycles_to_zero() : 0;
    if (const std::uint64_t adc = adc_.cycles_to_done(); adc != 0 && (next == 0 || adc < next)) next = adc;
    for (unsigned n = 0; n < timers_.size(); ++n) {
        const std::uint64_t edges = timers_[n].edges_to_event();
        if (edges == 0) continue;
        const std::uint64_t d = timer_divider(n);
        const std::uint64_t at = (cycles_ / d + edges) * d;
        if (next == 0 || at - cycles_ < next) next = at - cycles_;
    }
    return next;
}

void Lpc1768::advance_peripherals(std::uint64_t step) {
    const bool systick_due = systick_.interrupt_enabled() && systick_.cycles_to_zero() == step;
    systick_.advance(step);
    for (unsigned n = 0; n < timers_.size(); ++n) {
        const std::uint64_t d = timer_divider(n);
        timers_[n].advance((cycles_ + step) / d - cycles_ / d);
    }
    adc_.advance(step);
    cycles_ += step;
    if (systick_due) nvic_.pend_systick();
    unsigned channel = 0;
    std::uint32_t result = 0;
    if (adc_.take_completed(channel, result))
        trace_.record({.kind = TraceKind::AdcConversion, .value = result, .pin = channel}, cycles_);
    for (unsigned n = 0; n < timers_.size(); ++n)
        if (const std::uint32_t flags = timers_[n].take_new_flags())
            trace_.record({.kind = TraceKind::TimerMatch, .value = flags, .irq = kTimer0Irq + static_cast<int>(n)},
                          cycles_);
}

void Lpc1768::bind_handler(int irq, std::function<void()> handler) {
    if (irq < kSysTickIrq || irq >= kExternalIrqCount)
        throw std::out_of_range("no exception " + std::to_string(irq) + " to bind a handler to");
    handlers_[static_cast<std::size_t>(irq + 1)] = std::move(handler);
    service_interrupts();
}

void Lpc1768::set_external_level(unsigned port, unsigned pin, bool high) {
    const auto before = pending_snapshot();
    gpio_.set_external_level(port, pin, high);
    update_interrupt_lines();
    trace_new_pending(before);
    service_interrupts();
}

// Peripheral interrupt signals, recomputed from peripheral state after anything
// that can change it.
void Lpc1768::update_interrupt_lines() {
    nvic_.set_line(kAdcIrq, adc_.interrupt());
    for (unsigned n = 0; n < timers_.size(); ++n) nvic_.set_line(kTimer0Irq + static_cast<int>(n), timers_[n].interrupt());
}

void Lpc1768::service_interrupts() {
    if (in_handler_) return;  // no nesting: taken after the running handler returns
    bool took = false;
    for (;;) {
        const int irq =
            nvic_.next([this](int i) { return static_cast<bool>(handlers_[static_cast<std::size_t>(i + 1)]); });
        if (irq < kSysTickIrq) break;
        nvic_.enter(irq);
        trace_.record({.kind = TraceKind::Interrupt, .value = kInterruptEnter, .irq = irq}, cycles_);
        in_handler_ = true;
        try {
            handlers_[static_cast<std::size_t>(irq + 1)]();
        } catch (...) {
            in_handler_ = false;
            nvic_.exit(irq);
            throw;
        }
        in_handler_ = false;
        const auto before = pending_snapshot();
        nvic_.exit(irq);
        trace_.record({.kind = TraceKind::Interrupt, .value = kInterruptExit, .irq = irq}, cycles_);
        update_interrupt_lines();
        trace_new_pending(before);  // a level line still asserted pends again
        took = true;
    }
    if (took && thread_mode_ && !in_thread_mode_step_) {
        in_thread_mode_step_ = true;
        try {
            thread_mode_();
        } catch (...) {
            in_thread_mode_step_ = false;
            throw;
        }
        in_thread_mode_step_ = false;
    }
}

// [0] SysTick, [1 + n] IRQ n.
std::array<bool, kExternalIrqCount + 1> Lpc1768::pending_snapshot() const {
    std::array<bool, kExternalIrqCount + 1> pending{};
    for (int irq = kSysTickIrq; irq < kExternalIrqCount; ++irq)
        pending[static_cast<std::size_t>(irq + 1)] = nvic_.pending(irq);
    return pending;
}

void Lpc1768::trace_new_pending(const std::array<bool, kExternalIrqCount + 1>& before) {
    for (int irq = kSysTickIrq; irq < kExternalIrqCount; ++irq)
        if (nvic_.pending(irq) && !before[static_cast<std::size_t>(irq + 1)])
            trace_.record({.kind = TraceKind::Interrupt, .value = kInterruptPend, .irq = irq}, cycles_);
}

std::uint32_t Lpc1768::read(std::uint32_t address, unsigned size) {
    const std::uint32_t value = load(address, size);
    read_side_effects(address);
    trace_.record({.kind = TraceKind::Read, .address = address, .width = size, .value = value}, cycles_);
    update_interrupt_lines();  // a read can clear a peripheral's interrupt flag
    return value;
}

void Lpc1768::read_side_effects(std::uint32_t address) {
    if (address == kSysTickBase) systick_.read(SysTickReg::Ctrl);  // clears COUNTFLAG
    if (is_adc(address)) adc_.read_side_effects(address - kAdcBase);  // clears DONE flags
}

void Lpc1768::write(std::uint32_t address, unsigned size, std::uint32_t value) {
    const auto before = pending_snapshot();
    store(address, size, value);
    trace_.record({.kind = TraceKind::Write, .address = address, .width = size, .value = value}, cycles_);
    if (on_store_) on_store_();
    update_interrupt_lines();
    trace_new_pending(before);
    service_interrupts();  // taken at the instruction boundary after the store
}

std::uint32_t Lpc1768::peek32(std::uint32_t address) const { return load(address, 4); }

std::uint32_t Lpc1768::load(std::uint32_t address, unsigned size) const {
    if (is_alias(address)) {
        // Bit-band aliases are word accesses only here; narrow alias access is not modeled.
        if (size != 4 || (address & 3u) != 0) throw BusFault(address);
        const AliasTarget t = alias_target(address);
        return (read_target(address, t.word_address) >> t.bit) & 1u;
    }
    if (address == kPconpAddress) {
        if (size != 4) throw BusFault(address);
        return pconp_;
    }
    if (is_systick(address)) return systick_.peek(systick_reg(address, size));
    if (Nvic::maps(address)) return nvic_.read(address, size);
    if (address == kPclksel0Address || address == kPclksel1Address) {
        if (size != 4) throw BusFault(address);
        return pclksel_[address == kPclksel1Address];
    }
    std::uint32_t offset = 0;
    if (const int n = timer_at(address, offset); n >= 0)
        return timers_[static_cast<std::size_t>(n)].read(timer_reg(address, offset, size));
    if (is_adc(address)) return adc_.peek(adc_offset(address, size));
    const GpioTarget t = decode_gpio(address, size);
    return (gpio_.read(t.port, t.reg) & t.lanes) >> t.shift;
}

void Lpc1768::store(std::uint32_t address, unsigned size, std::uint32_t value) {
    if (is_alias(address)) {
        if (size != 4 || (address & 3u) != 0) throw BusFault(address);
        const AliasTarget t = alias_target(address);
        const std::uint32_t word = read_target(address, t.word_address);
        const std::uint32_t mask = std::uint32_t{1} << t.bit;
        store(t.word_address, 4, (value & 1u) ? (word | mask) : (word & ~mask));
        return;
    }
    if (address == kPconpAddress) {
        if (size != 4) throw BusFault(address);
        pconp_ = value;
        return;
    }
    if (is_systick(address)) {
        const SysTickReg reg = systick_reg(address, size);
        // STCALIB is read-only (ARM DUI 0552A); UM10360 Table 438 says R/W. With the
        // sources disagreeing, a write is reported rather than guessed at.
        if (reg == SysTickReg::Calib) throw BusFault(address);
        systick_.write(reg, value);
        return;
    }
    if (Nvic::maps(address)) return nvic_.write(address, size, value);
    if (address == kPclksel0Address || address == kPclksel1Address) {
        if (size != 4) throw BusFault(address);
        pclksel_[address == kPclksel1Address] = value;
        return;
    }
    std::uint32_t offset = 0;
    if (const int n = timer_at(address, offset); n >= 0)
        return timers_[static_cast<std::size_t>(n)].write(timer_reg(address, offset, size), value);
    if (is_adc(address)) {
        const std::uint32_t reg = adc_offset(address, size);
        if (reg >= 0x10) throw BusFault(address);  // ADDRn, ADSTAT: read-only
        if (reg == 0x00 && Adc::unsupported_control(value))
            throw NotModelled(address, "ADC burst mode or edge-triggered START");
        return adc_.write(reg, value, adc_conversion_cycles_for(value));
    }
    const GpioTarget t = decode_gpio(address, size);
    gpio_.write(t.port, t.reg, (value << t.shift) & t.lanes, t.lanes);
}

}  // namespace latasim::lpc17xx
