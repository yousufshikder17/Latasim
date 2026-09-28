#pragma once
// The MCB1700 graphic LCD: a 240 x 320 TFT whose controller is driven over SPI
// (SSP1) with chip select on P0.6 (docs/phase4/overview.md, "GLCD").
//
// Keil's GLCD_MCB1700.c supports two controllers: HX8347-D, which answers a
// bit-banged ID read, and an ILI9320-family controller otherwise. Which one a
// given board carries is not in the pack's documents; this models the
// ILI9320-family path, the one the driver takes when the bit-banged read gets no
// answer, and the one the LPC1768 simulator exercised (E6).
//
// Serial protocol, as the driver uses it: while CS is low, a start byte 0x70 |
// RS << 1 | RW, then 16-bit words, most significant byte first. RS = 0, RW = 0
// writes the index register; RS = 1, RW = 0 writes data to the indexed register;
// RS = 1, RW = 1 reads it (after one dummy byte). Data to R22 goes to GRAM at the
// address counter, which then moves as entry mode R03 says (I/D bits 5:4 up or
// down, AM bit 3: vertical first) inside the window R50-R53 (horizontal start/end,
// vertical start/end); R20/R21 set the counter. Other registers are stored.
//
// Not modelled: the HX8347-D path, the RS/RW-less 3-wire bit-banged ID read
// (P0.9 reads its idle level, so the driver sees 0xFF), display timing, power and
// gamma settings (stored only), panel scan direction and mirroring (R01, R60:
// pixel() uses the driver's own coordinates), and GRAM contents at power-up
// (black here).
#include <array>
#include <cstdint>
#include <vector>

namespace latasim::mcb1700 {

class Glcd {
public:
    static constexpr unsigned kGramWidth = 240;   // horizontal addresses (R20)
    static constexpr unsigned kGramHeight = 320;  // vertical addresses (R21)
    // The driver's screen (GLCD_Config.h: 320 x 240 with GLCD_SWAP_XY = 1).
    static constexpr unsigned kWidth = 320;
    static constexpr unsigned kHeight = 240;
    static constexpr std::uint16_t kId = 0x9320;  // R00, as an ILI9320 reads it

    Glcd();  // window reset to the full GRAM (R51 = 0xEF, R53 = 0x13F)

    // The chip select line changed (low = selected). Ends any transaction.
    void chip_select(bool selected);
    // One byte shifted in while selected; returns the byte shifted out.
    std::uint8_t shift(std::uint8_t mosi);

    std::uint16_t gram(unsigned h, unsigned v) const { return gram_.at(v * kGramWidth + h); }
    // A pixel in the driver's coordinates: x across 320, y down 240.
    std::uint16_t pixel(unsigned x, unsigned y) const { return gram(y, x); }
    std::uint16_t reg(std::uint8_t index) const { return regs_[index]; }
    // An FNV-1a hash of GRAM, for compact golden checks.
    std::uint64_t hash() const;
    unsigned bad_start_bytes() const { return bad_start_bytes_; }

    // What happened since the last call, for the trace: register writes (index,
    // value) and GRAM bursts ended by chip select (pixel counts).
    struct Write {
        std::uint8_t index;
        std::uint32_t value;  // for R22 bursts: the pixel count
    };
    std::vector<Write> take_writes();

private:
    enum class Phase { Idle, Start, Index, Data, Read, Ignore };
    void write_data(std::uint16_t value);
    void advance_address();

    std::array<std::uint16_t, kGramWidth * kGramHeight> gram_{};
    std::array<std::uint16_t, 256> regs_{};
    Phase phase_ = Phase::Idle;
    bool high_byte_ = true;
    std::uint16_t word_ = 0;
    std::uint8_t index_ = 0;
    unsigned h_ = 0, v_ = 0;  // address counter
    bool selected_ = false;
    bool dummy_ = false;
    unsigned burst_ = 0;
    unsigned bad_start_bytes_ = 0;
    std::vector<Write> writes_;
};

}  // namespace latasim::mcb1700
