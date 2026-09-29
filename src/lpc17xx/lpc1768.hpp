#pragma once
// The LPC1768 as firmware sees it: 8/16/32-bit loads and stores to its memory map.
// This is the seam a future MMIO adapter (host-compiled firmware, emulator)
// plugs into. Mapped so far: the GPIO block and its bit-band alias, the pin connect
// block, PCONP, PCLKSEL0-1, EXTINT/EXTMODE/EXTPOLAR, SysTick, the NVIC, Timer 0-3,
// the ADC and SSP1; anything else raises BusFault, as an unmapped access would on the chip. A mapped register used in a mode the model
// does not implement (ADC burst mode, ...) raises NotModelled.
#include <array>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>

#include "lpc17xx/adc.hpp"
#include "lpc17xx/eint.hpp"
#include "lpc17xx/gpio.hpp"
#include "lpc17xx/nvic.hpp"
#include "lpc17xx/ssp.hpp"
#include "lpc17xx/systick.hpp"
#include "lpc17xx/timer.hpp"
#include "lpc17xx/usb_device.hpp"
#include "trace/trace.hpp"

namespace latasim::lpc17xx {

// From LPC17xx.h (LPC1700_DFP 2.6.0).
inline constexpr std::uint32_t kGpioBase = 0x2009C000;  // LPC_GPIO_BASE
inline constexpr std::uint32_t kGpioPortStride = 0x20;  // LPC_GPIOn_BASE = base + n * 0x20

// PCONP (UM10360 Table 46). Reset value: the sum of Table 46's per-bit reset values,
// which the LPC1768 simulator also reads at reset (E9) and Keil's SystemInit writes.
// UM10360 Table 14 and the SVD give 0x03BE instead, which contradicts Table 46.
inline constexpr std::uint32_t kPconpAddress = 0x400FC0C4;
inline constexpr std::uint32_t kPconpReset = 0x042887DE;
inline constexpr std::uint32_t kPconpGpio = std::uint32_t{1} << 15;  // PCGPIO

// Pin connect block (UM10360 chapter 8): PINSEL0-10, PINMODE0-9, PINMODE_OD0-4 and
// I2CPADCFG at 0x4002C000-0x4002C07C, reset 0. Stored; the only function selection
// the model acts on is P2.10's EINT0 (PINSEL4[21:20] = 01). The reserved words
// (PINSEL5-6, 0x2C-0x3C, PINMODE8) read 0 and ignore writes: Keil's PIN_Configure
// touches PINMODE8 for port 4. Pull resistors (PINMODE) have no effect on levels.
inline constexpr std::uint32_t kPinconBase = 0x4002C000;
inline constexpr std::uint32_t kPinconWindow = 0x80;

// PCLKSEL0/1 (UM10360 tables 40, 41): storage, reset 0, and the peripheral clock
// dividers the modelled peripherals use (00: CCLK/4, 01: CCLK, 10: CCLK/2, 11: CCLK/8).
inline constexpr std::uint32_t kPclksel0Address = 0x400FC1A8;
inline constexpr std::uint32_t kPclksel1Address = 0x400FC1AC;

// The core clock (CCLK) that virtual time counts. Keil's SystemInit for the MCB1700
// sets PLL0 from the 12 MHz crystal to M = 100, N = 6 and divides by 4:
// 2 * 12 MHz * 100 / 6 / 4 = 100 MHz; the LPC1768 simulator reports CCLK=100000000
// after it (E2), and STCALIB's 10 ms value assumes it. There is no clock-tree model:
// firmware that reprograms the PLL does not change this.
inline constexpr std::uint64_t kCoreClockHz = 100'000'000;
inline constexpr std::uint64_t kCyclesPerMillisecond = kCoreClockHz / 1000;

// D/A converter (UM10360 chapter 30): DACR (VALUE bits 15:6, BIAS bit 16),
// DACCTRL and DACCNTVAL are stored; each DACR store sets the output. The DMA and
// double-buffer counter modes are not modelled (DACCTRL is storage only).
inline constexpr std::uint32_t kDacBase = 0x4008C000;

// A USB host plugged into the device port (devices/usb.hpp) is given the bus once
// per 1 ms frame.
inline constexpr std::uint64_t kUsbFrameCycles = 100'000;  // 1 ms at 100 MHz

// Cortex-M3 SRAM bit-band: each bit of 0x20000000-0x200FFFFF has a word alias.
inline constexpr std::uint32_t kBitBandBase = 0x20000000;
inline constexpr std::uint32_t kBitBandAliasBase = 0x22000000;
inline constexpr std::uint32_t kBitBandSize = 0x00100000;

constexpr std::uint32_t gpio_register_address(unsigned port, GpioReg reg) {
    return kGpioBase + port * kGpioPortStride + static_cast<std::uint32_t>(reg);
}

// alias = 0x22000000 + (address - 0x20000000) * 32 + bit * 4, where address is
// the byte holding the bit; for a 32-bit register, bit 0-31 of the word.
constexpr std::uint32_t bit_band_alias(std::uint32_t word_address, unsigned bit) {
    const std::uint32_t byte_address = word_address + bit / 8;
    return kBitBandAliasBase + (byte_address - kBitBandBase) * 32 + (bit % 8) * 4;
}

class BusFault : public std::runtime_error {
public:
    explicit BusFault(std::uint32_t address);
    std::uint32_t address() const { return address_; }

private:
    std::uint32_t address_;
};

// A register access the model does not implement, e.g. starting the ADC in burst
// mode: explicit, rather than silently doing something else.
class NotModelled : public std::runtime_error {
public:
    NotModelled(std::uint32_t address, const std::string& what);
};

class Lpc1768 {
public:
    // Loads and stores of 8, 16 and 32 bits. Narrow GPIO accesses address the byte
    // and halfword registers of LPC17xx.h (FIO1PIN0, FIO1SETH, ...) and must be
    // naturally aligned. Bit-band aliases accept 32-bit accesses only.
    // Loads are not const: some registers change when read (STCTRL's COUNTFLAG).
    std::uint8_t read8(std::uint32_t address) { return static_cast<std::uint8_t>(read(address, 1)); }
    std::uint16_t read16(std::uint32_t address) { return static_cast<std::uint16_t>(read(address, 2)); }
    std::uint32_t read32(std::uint32_t address) { return read(address, 4); }
    void write8(std::uint32_t address, std::uint8_t value) { write(address, 1, value); }
    void write16(std::uint32_t address, std::uint16_t value) { write(address, 2, value); }
    // Bit-band alias writes are a read-modify-write of the whole target word, as
    // on the Cortex-M3. For FIOPIN that copies input pin levels into the latch.
    void write32(std::uint32_t address, std::uint32_t value) { write(address, 4, value); }

    Gpio& gpio() { return gpio_; }
    const Gpio& gpio() const { return gpio_; }

    // For observers (tests, assertions): the value a 32-bit load would return, with
    // no side effects and no trace event. Reading STCTRL this way leaves COUNTFLAG
    // set. Faults like a load.
    std::uint32_t peek32(std::uint32_t address) const;

    // Every successful load and store above is traced, with the address and width
    // the firmware used; a faulting access records nothing. A bit-band alias access
    // is one event at the alias address.
    Trace& trace() { return trace_; }
    const Trace& trace() const { return trace_; }

    // Called after every successful store, so the board can trace what it shows.
    void on_store(std::function<void()> hook) { on_store_ = std::move(hook); }

    // Virtual time, in core clock cycles since the machine was created. It moves
    // only when advance_cycles is called: executing firmware on the host takes no
    // virtual time, and nothing here reads a wall clock. SysTick counts these cycles.
    std::uint64_t cycles() const { return cycles_; }
    void advance_cycles(std::uint64_t cycles);
    // Cycles until the next thing scheduled to happen (a SysTick count to 0, a timer
    // match, an ADC completion), 0 if nothing is scheduled.
    std::uint64_t cycles_to_next_event() const;
    // True while an interrupt handler runs.
    bool in_handler() const { return in_handler_; }

    // Interrupt delivery (docs/phase4/overview.md). The host binds each exception's
    // handler by CMSIS IRQ number (kSysTickIrq, kTimer0Irq, ...); the model never
    // names firmware symbols. An exception is taken when it is pending, enabled and
    // the highest priority (Nvic::next), and has a bound handler; without one it
    // stays pending. Handlers run synchronously on the host and take no virtual
    // time, at the virtual time of the event that made them pending: after a time
    // step, after an MMIO store, after an input change. There is no nesting: an
    // exception that becomes pending during a handler is taken after it returns
    // (tail-chained), in priority order. Advancing time from a handler is an error.
    void bind_handler(int irq, std::function<void()> handler);
    // Called each time the processor returns to thread mode after taking one or
    // more exceptions: the place for a firmware main loop's reaction to flags its
    // handlers set, since the host does not run main loops continuously.
    void on_thread_mode(std::function<void()> step) { thread_mode_ = std::move(step); }
    const Nvic& nvic() const { return nvic_; }

    // An analog input's level, as a 12-bit conversion result (0 = VREFN, 0xFFF =
    // VREFP), from the board.
    void set_analog_input(unsigned channel, std::uint32_t raw) { adc_.set_input(channel, raw); }
    const Adc& adc() const { return adc_; }
    // Core cycles one ADC conversion takes: 65 ADC clocks of PCLK_ADC / (CLKDIV + 1).
    std::uint64_t adc_conversion_cycles() const;

    const ExternalInterrupt& external_interrupt() const { return eint_; }
    std::uint32_t pincon(std::uint32_t offset) const { return pincon_.at(offset / 4); }

    // Timer n (0-3) for observation, and the core cycles per PCLK edge it runs at.
    const Timer& timer(unsigned n) const { return timers_.at(n); }
    std::uint32_t timer_divider(unsigned n) const;

    // A pin's level driven from outside (board inputs). Updates GPIO and anything
    // watching the pin, then takes any exception that became due.
    void set_external_level(unsigned port, unsigned pin, bool high);

    // SysTick at 0xE000E010-0xE000E01F: 32-bit accesses only; CALIB is read-only.
    // For observation; firmware goes through the loads and stores above.
    const SysTick& systick() const { return systick_; }

    // The DAC's output, VALUE (0-1023), and a hook called whenever a store sets it.
    std::uint32_t dac_value() const { return (dac_[0] >> 6) & 0x3FFu; }
    void on_dac_output(std::function<void(std::uint32_t value)> hook) { on_dac_ = std::move(hook); }

    // The USB device controller, and the host plugged into its port. Attaching a
    // host plugs the cable in (the device sees VBUS) and gives the host a frame every
    // kUsbFrameCycles of virtual time, on multiples of it; nullptr unplugs it.
    UsbDevice& usb() { return usb_; }
    const UsbDevice& usb() const { return usb_; }
    void attach_usb_host(usb::HostPort* host);

    // PCONP is storage only: 32-bit access, and no bit gates anything. GPIO keeps
    // working with PCGPIO clear, as in the simulator (E9) and UM10360 section 9.1
    // ("Power: always enabled"); docs/phase2/open-questions.md.
    std::uint32_t pconp() const { return pconp_; }

    // SSP1 (ssp.hpp) and the device wired to its bus. Each DR store sends a frame to
    // `peer` inside the store, before it is traced; the peer must not access MMIO.
    const Ssp& ssp1() const { return ssp1_; }
    void attach_ssp1(Ssp::Peer peer) { ssp1_.attach(std::move(peer)); }

    // Core cycles the code that made the stores since the last call would have
    // spent waiting for a peripheral: each SSP1 frame's time on the wire, at
    // PCLK_SSP1 (PCLKSEL0[21:20]). The model completes such transfers at once and
    // never advances time for them; host firmware is charged these cycles.
    std::uint64_t take_stall_cycles() { return std::exchange(stall_cycles_, std::uint64_t{0}); }

private:
    struct GpioTarget {
        unsigned port;
        GpioReg reg;
        unsigned shift;       // bit position of the accessed lane(s)
        std::uint32_t lanes;  // the bits the access covers
    };
    static GpioTarget decode_gpio(std::uint32_t address, unsigned size);
    std::uint32_t read(std::uint32_t address, unsigned size);  // traced
    void write(std::uint32_t address, unsigned size, std::uint32_t value);  // traced
    std::uint32_t load(std::uint32_t address, unsigned size) const;  // no side effects
    void read_side_effects(std::uint32_t address);
    void store(std::uint32_t address, unsigned size, std::uint32_t value);
    std::uint32_t read_target(std::uint32_t alias, std::uint32_t word_address) const;
    void update_interrupt_lines();
    void advance_peripherals(std::uint64_t step);
    std::uint64_t adc_conversion_cycles_for(std::uint32_t adcr) const;
    void service_interrupts();
    std::array<bool, kExternalIrqCount + 1> pending_snapshot() const;
    void trace_new_pending(const std::array<bool, kExternalIrqCount + 1>& before);

    Gpio gpio_;
    std::uint32_t pconp_ = kPconpReset;
    std::uint64_t cycles_ = 0;
    SysTick systick_;
    Nvic nvic_;
    std::array<Timer, 4> timers_{};
    Adc adc_;
    ExternalInterrupt eint_;
    std::array<std::uint32_t, kPinconWindow / 4> pincon_{};
    std::array<std::uint32_t, 2> pclksel_{};
    std::array<std::uint32_t, 3> dac_{};  // DACR, DACCTRL, DACCNTVAL
    std::function<void(std::uint32_t)> on_dac_;
    UsbDevice usb_;
    usb::HostPort* usb_host_ = nullptr;
    Ssp ssp1_;
    std::uint64_t stall_cycles_ = 0;
    std::array<std::function<void()>, kExternalIrqCount + 1> handlers_;  // [irq + 1]
    std::function<void()> thread_mode_;
    bool in_handler_ = false;
    bool in_thread_mode_step_ = false;
    Trace trace_;
    std::function<void()> on_store_;
};

}  // namespace latasim::lpc17xx
