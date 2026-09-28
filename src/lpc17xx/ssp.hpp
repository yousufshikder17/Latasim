#pragma once
// The LPC1768's SSP1 controller (UM10360 chapter 18), as far as a polled SPI master
// uses it: write a frame to DR, wait for SR.RNE, read the reply from DR. Keil's
// legacy GLCD driver (GLCD_SPI_LPC1700.c) drives the MCB1700's display this way.
//
// Registers (Table 369): CR0, CR1 and CPSR are stored (reset 0; CPSR bit 0 reads
// 0, Table 374). SR is read-only (Table 373). A DR write while enabled (CR1.SSE)
// in master mode sends one frame at once: what the peer on the bus answers goes
// into the 8-frame receive FIFO. So SR always reads TFE = 1, TNF = 1, BSY = 0, and
// RNE and RFF follow the receive FIFO. A DR read takes the oldest received frame.
//
// Model choices, where the manual is silent: a DR read with the receive FIFO empty
// returns 0; with no peer attached (a bare Lpc1768) frames go nowhere and 0xFF
// comes back.
//
// Not modelled (Lpc1768 raises NotModelled): frames other than 8-bit SPI (CR0 DSS,
// FRF), slave mode, loopback, DR writes while disabled (they would wait in the
// transmit FIFO), receive overrun (the manual leaves open which frame is kept),
// the interrupt and DMA registers (IMSC, RIS, MIS, ICR, DMACR), and SSP0. Frames
// take no virtual time: the bit rate (CPSR, CR0 SCR, PCLKSEL0) is stored, not
// applied.
#include <cstdint>
#include <deque>
#include <functional>

namespace latasim::lpc17xx {

inline constexpr std::uint32_t kSsp1Base = 0x40030000;
inline constexpr std::uint32_t kSspWindow = 0x28;  // CR0 .. DMACR
inline constexpr unsigned kSspFifoFrames = 8;

// SR bits (Table 373).
inline constexpr std::uint32_t kSspTfe = 1u << 0;
inline constexpr std::uint32_t kSspTnf = 1u << 1;
inline constexpr std::uint32_t kSspRne = 1u << 2;
inline constexpr std::uint32_t kSspRff = 1u << 3;

enum class SspReg : std::uint32_t { Cr0 = 0x00, Cr1 = 0x04, Dr = 0x08, Sr = 0x0C, Cpsr = 0x10 };

class Ssp {
public:
    // The device on the bus: given each frame sent, returns the frame it shifts back.
    using Peer = std::function<std::uint8_t(std::uint8_t)>;
    void attach(Peer peer) { peer_ = std::move(peer); }

    // CR0, CR1, DR, SR, CPSR; the rest of the window is not modelled.
    static bool modelled(std::uint32_t offset) { return offset <= 0x10 && offset % 4 == 0; }

    std::uint32_t peek(std::uint32_t offset) const;
    void read_side_effects(std::uint32_t offset);  // DR: the frame read leaves the FIFO
    // Why a DR write cannot be modelled in the current configuration, or nullptr.
    const char* cannot_send() const;
    void write(std::uint32_t offset, std::uint32_t value);  // not SR (read-only)

private:
    std::uint32_t cr0_ = 0, cr1_ = 0, cpsr_ = 0;
    std::deque<std::uint8_t> rx_;
    Peer peer_;
};

}  // namespace latasim::lpc17xx
