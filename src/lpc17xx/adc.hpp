#pragma once
// The LPC1768 A/D converter in software-controlled mode (UM10360 chapter 29).
//
// Registers: ADCR (reset 0x01), ADGDR, ADINTEN (reset 0x100), ADDR0-ADDR7, ADSTAT.
// Writing ADCR with START = 001 and PDN = 1 starts a conversion of the lowest
// selected channel (SEL = 0 counts as channel 0); it completes 65 ADC clocks
// later (29.5.1), the ADC clock being PCLK_ADC / (CLKDIV + 1). The input is
// sampled when the conversion starts. On completion the result goes to ADGDR
// (RESULT, CHN, DONE) and the channel's ADDRn (RESULT, DONE). DONE in ADGDR is
// cleared by reading ADGDR or writing ADCR; a channel's DONE by reading its
// ADDRn (29.5.2, 29.5.4). The interrupt line is the global DONE when ADGINTEN is
// set, otherwise any channel DONE enabled in ADINTEN (29.5.3, 29.5.5).
//
// Not modelled: burst mode, edge-triggered starts (START 010-111), ADTRM, DMA;
// Lpc1768 rejects them. OVERRUN only occurs in burst mode, so it always reads 0.
// Writing ADCR during a conversion with START = 001 restarts it; with START = 000
// it lets it finish.
#include <array>
#include <cstdint>

namespace latasim::lpc17xx {

inline constexpr std::uint32_t kAdcBase = 0x40034000;
inline constexpr std::uint32_t kAdcConversionClocks = 65;
inline constexpr std::uint32_t kAdcMaxInput = 0xFFF;  // 12-bit result

enum class AdcReg : std::uint32_t {
    Adcr = 0x00, Adgdr = 0x04, Adinten = 0x0C, Addr0 = 0x10, Adstat = 0x30,
};

class Adc {
public:
    // Offsets of modelled registers: ADCR, ADGDR, ADINTEN, ADDR0-7, ADSTAT.
    static bool modelled(std::uint32_t offset);
    // Rejected ADCR values: burst mode or an edge-triggered START.
    static bool unsupported_control(std::uint32_t adcr);

    std::uint32_t peek(std::uint32_t offset) const;
    void read_side_effects(std::uint32_t offset);
    // `conversion_cycles`: core cycles a conversion takes at the current clocks.
    void write(std::uint32_t offset, std::uint32_t value, std::uint64_t conversion_cycles);

    void set_input(unsigned channel, std::uint32_t raw);  // 0 .. kAdcMaxInput
    std::uint32_t input(unsigned channel) const { return inputs_.at(channel); }
    std::uint32_t clock_divider() const { return ((adcr_ >> 8) & 0xFFu) + 1; }  // CLKDIV + 1

    std::uint64_t cycles_to_done() const { return busy_ ? remaining_ : 0; }  // 0 = none
    void advance(std::uint64_t cycles);
    bool interrupt() const;
    bool busy() const { return busy_; }
    // True once per completed conversion (for tracing); `channel` and `result` set.
    bool take_completed(unsigned& channel, std::uint32_t& result);

private:
    std::uint32_t adcr_ = 0x01;
    std::uint32_t adinten_ = 0x100;
    std::uint32_t global_result_ = 0, global_channel_ = 0;
    bool global_done_ = false;
    std::array<std::uint32_t, 8> results_{};
    std::array<bool, 8> done_{};
    std::array<std::uint32_t, 8> inputs_{};
    bool busy_ = false;
    std::uint64_t remaining_ = 0;
    unsigned converting_ = 0;
    std::uint32_t sample_ = 0;
    bool completed_ = false;
};

}  // namespace latasim::lpc17xx
