#pragma once
// The Cortex-M3 NVIC as the LPC1768 uses it, plus the SysTick exception: the
// interrupt state Latasim delivers from (docs/phase4/overview.md).
// Sources: ARM DUI 0552A sections 2.3 and 4.2; LPC17xx.h for IRQ numbers and
// __NVIC_PRIO_BITS.
//
// Per exception: enable (always on for SysTick), pending, active and an 8-bit
// priority of which the top 5 bits are implemented. Peripheral interrupts are
// level-sensitive lines: an asserted line makes its IRQ pending while not active,
// and an IRQ whose line is still asserted when its handler returns is pending
// again (ARM 4.2.9). SysTick is pended by the timer's count to 0.
//
// Registers: ISER0-1, ICER0-1, ISPR0-1, ICPR0-1, IABR0-1 (word access) and
// IPR0-8 (byte, halfword or word), and SHPR3's SysTick byte. Not modelled:
// PRIGROUP and subpriorities (all 5 bits are group priority at reset), STIR,
// PRIMASK/FAULTMASK/BASEPRI, and the other system exceptions.
#include <array>
#include <bitset>
#include <cstdint>
#include <string>

namespace latasim::lpc17xx {

// CMSIS IRQn_Type values (LPC17xx.h); SysTick is the core exception -1.
inline constexpr int kSysTickIrq = -1;
inline constexpr int kTimer0Irq = 1;
inline constexpr int kTimer1Irq = 2;
inline constexpr int kTimer2Irq = 3;
inline constexpr int kTimer3Irq = 4;
inline constexpr int kEint0Irq = 18;
inline constexpr int kAdcIrq = 22;
inline constexpr int kExternalIrqCount = 35;  // WDT_IRQn (0) to CANActivity_IRQn (34)
inline constexpr unsigned kPriorityBits = 5;  // __NVIC_PRIO_BITS
inline constexpr std::uint8_t kPriorityMask = static_cast<std::uint8_t>(0xFF << (8 - kPriorityBits));

// "SysTick", "TIMER0", "ADC", ... or "IRQ<n>".
std::string irq_name(int irq);

class Nvic {
public:
    static bool maps(std::uint32_t address);
    // Faults (BusFault) on unmapped addresses and on word-only registers accessed
    // narrowly or misaligned.
    std::uint32_t read(std::uint32_t address, unsigned size) const;
    void write(std::uint32_t address, unsigned size, std::uint32_t value);

    // A peripheral's interrupt signal.
    void set_line(int irq, bool asserted);
    void pend_systick() { systick_pending_ = true; }

    // The highest-priority exception that can be taken now: pending, enabled
    // (SysTick always is) and not active; lowest priority value first, then lowest
    // exception number (SysTick is exception 15, IRQ n is 16 + n). -2 if none.
    // `takeable(irq)` filters further (the host: "has a handler").
    template <class Takeable>
    int next(Takeable takeable) const;

    void enter(int irq);  // pending -> active
    void exit(int irq);   // not active; a still-asserted line pends again

    bool enabled(int irq) const { return irq == kSysTickIrq || enabled_[index(irq)]; }
    bool pending(int irq) const { return irq == kSysTickIrq ? systick_pending_ : pending_[index(irq)]; }
    bool active(int irq) const { return irq == kSysTickIrq ? systick_active_ : active_[index(irq)]; }
    std::uint8_t priority(int irq) const { return irq == kSysTickIrq ? systick_priority_ : priority_[index(irq)]; }

private:
    static std::size_t index(int irq);
    void sample(std::size_t i) {
        if (line_[i] && !active_[i]) pending_[i] = true;
    }

    std::bitset<kExternalIrqCount> enabled_, pending_, active_, line_;
    std::array<std::uint8_t, kExternalIrqCount> priority_{};
    bool systick_pending_ = false;
    bool systick_active_ = false;
    std::uint8_t systick_priority_ = 0;
};

template <class Takeable>
int Nvic::next(Takeable takeable) const {
    int best = -2;
    unsigned best_priority = 0x100;
    auto consider = [&](int irq) {
        if (!pending(irq) || !enabled(irq) || active(irq) || !takeable(irq)) return;
        if (priority(irq) < best_priority) {  // ties keep the lower exception number
            best = irq;
            best_priority = priority(irq);
        }
    };
    consider(kSysTickIrq);
    for (int irq = 0; irq < kExternalIrqCount; ++irq) consider(irq);
    return best;
}

}  // namespace latasim::lpc17xx
