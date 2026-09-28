#pragma once
// The LPC1768 USB device controller at its digital interface (UM10360 chapter 11),
// in slave mode (no DMA): what Keil's USBD_LPC17xx.c driver uses.
//
// Firmware side (registers at 0x5000C200-0x5000C24C, 0x5000CFF4-0x5000CFF8):
//   USBDevIntSt/En/Clr/Set, USBCmdCode/USBCmdData (the SIE command protocol:
//   Set Address, Configure Device, Set Mode, Read Frame Number, Read Test Register,
//   Set/Get Device Status, Get Error Code, Read Error Status, Select Endpoint,
//   Select Endpoint/Clear Interrupt, Set Endpoint Status, Clear Buffer, Validate
//   Buffer), USBRxData/USBRxPLen/USBTxData/USBTxPLen/USBCtrl (packet FIFOs),
//   USBEpIntSt/En/Clr/Set/Pri, USBReEp/USBEpInd/USBMaxPSize (realization),
//   USBDevIntPri, USBClkCtrl/USBClkSt. SIE commands complete at once: CCEMPTY and
//   CDFULL are set by the store that issues them.
//
// Bus side (usb::Bus): a virtual host sends SETUP/OUT packets, takes IN packets,
// delivers isochronous data and marks frames. Transfers are whole packets; there is
// no bit timing, PHY, NRZI, CRC, toggle or handshake timing. Each endpoint has one
// buffer (double buffering of bulk/isochronous endpoints is not modelled).
//
// Not modelled: DMA (UDCA, DMA registers), the host and OTG controllers, remote
// wakeup, suspend timing, errors. Their registers fault.
#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "devices/usb.hpp"

namespace latasim::lpc17xx {

inline constexpr std::uint32_t kUsbBase = 0x5000C000;
inline constexpr int kUsbIrq = 24;

// USBDevIntSt bits.
inline constexpr std::uint32_t kUsbFrameInt = 1u << 0;
inline constexpr std::uint32_t kUsbEpSlowInt = 1u << 2;
inline constexpr std::uint32_t kUsbDevStatInt = 1u << 3;
inline constexpr std::uint32_t kUsbCcEmpty = 1u << 4;
inline constexpr std::uint32_t kUsbCdFull = 1u << 5;
inline constexpr std::uint32_t kUsbEpRealized = 1u << 8;

class UsbDevice : public usb::Bus {
public:
    UsbDevice();

    static bool modelled(std::uint32_t offset);  // offset from kUsbBase
    std::uint32_t peek(std::uint32_t offset) const;
    void read_side_effects(std::uint32_t offset);  // RxData consumes the FIFO
    void write(std::uint32_t offset, std::uint32_t value);
    bool interrupt() const;

    // ---- usb::Bus ----
    bool pull_up() const override { return (dev_status_ & kCon) != 0; }
    void cable(bool attached) override;
    void bus_reset() override;
    void start_of_frame() override;
    void setup(const std::array<std::uint8_t, 8>& packet) override;
    std::optional<std::vector<std::uint8_t>> in(unsigned ep) override;
    bool control_stalled() const override { return eps_[1].stalled; }
    bool out(unsigned ep, const std::vector<std::uint8_t>& data) override;
    void iso_out(unsigned ep, const std::vector<std::uint8_t>& data) override;

    // Observation.
    bool attached() const { return attached_; }
    bool configured() const { return configured_; }
    unsigned address() const { return address_ & 0x7Fu; }
    unsigned frame_number() const { return frame_; }
    bool realized(unsigned physical_ep) const { return (re_ep_ >> physical_ep) & 1u; }

private:
    struct Endpoint {
        std::uint16_t max_packet = 8;
        bool stalled = false;
        bool disabled = false;
        bool full = false;  // OUT: holds a received packet; IN: holds a validated packet
        bool setup = false;
        bool overwritten = false;
        std::vector<std::uint8_t> data;
    };
    static constexpr std::uint8_t kCon = 1, kConCh = 2, kSus = 4, kSusCh = 8, kRst = 16;

    static bool iso(unsigned physical_ep);
    void command(std::uint32_t value);
    void command_write(std::uint8_t data);
    std::uint8_t command_read();
    std::uint8_t select_status(unsigned physical_ep) const;
    void ep_interrupt(unsigned physical_ep);
    void dev_status_changed(std::uint8_t change);

    std::uint32_t dev_int_st_ = kUsbCcEmpty;
    std::uint32_t dev_int_en_ = 0;
    std::uint32_t ep_int_st_ = 0;
    std::uint32_t ep_int_en_ = 0;
    std::uint32_t ep_int_pri_ = 0;
    std::uint32_t re_ep_ = 3;  // control endpoints realized at reset
    std::uint32_t ep_ind_ = 0;
    std::uint32_t clk_ctrl_ = 0;
    std::uint32_t cmd_data_ = 0;
    std::uint32_t ctrl_ = 0;
    std::uint32_t tx_len_ = 0;
    std::array<Endpoint, 32> eps_{};
    // Current SIE command awaiting its data phase, and the selected endpoint.
    std::uint8_t pending_cmd_ = 0;
    bool has_pending_cmd_ = false;
    unsigned selected_ = 0;
    unsigned frame_read_ = 0;  // Read Frame Number: bytes returned so far
    std::uint8_t dev_status_ = 0;
    std::uint8_t address_ = 0;
    bool configured_ = false;
    bool attached_ = false;
    unsigned frame_ = 0;
    std::size_t rx_pos_ = 0;  // bytes of the selected OUT packet read so far
    std::vector<std::uint8_t> tx_;
    std::array<std::optional<std::vector<std::uint8_t>>, 32> iso_next_{};  // arrives this frame
};

}  // namespace latasim::lpc17xx
