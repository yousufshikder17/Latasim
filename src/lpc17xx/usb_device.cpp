#include "lpc17xx/usb_device.hpp"

namespace latasim::lpc17xx {
namespace {

// Register offsets from kUsbBase (USBD_LPC17xx.h layout; UM10360 table 192).
enum : std::uint32_t {
    kDevIntSt = 0x200, kDevIntEn = 0x204, kDevIntClr = 0x208, kDevIntSet = 0x20C,
    kCmdCode = 0x210, kCmdData = 0x214, kRxData = 0x218, kTxData = 0x21C,
    kRxPLen = 0x220, kTxPLen = 0x224, kCtrl = 0x228, kDevIntPri = 0x22C,
    kEpIntSt = 0x230, kEpIntEn = 0x234, kEpIntClr = 0x238, kEpIntSet = 0x23C, kEpIntPri = 0x240,
    kReEp = 0x244, kEpInd = 0x248, kMaxPSize = 0x24C,
    kClkCtrl = 0xFF4, kClkSt = 0xFF8,
};

constexpr std::uint32_t kRdEn = 1u, kWrEn = 2u;
constexpr std::uint32_t kPktDv = 1u << 10, kPktRdy = 1u << 11;
constexpr std::uint32_t kIsoMask = 0x030C30C0u;  // physical endpoints 6, 7, 12, 13, 18, 19, 24, 25

}  // namespace

UsbDevice::UsbDevice() = default;

bool UsbDevice::iso(unsigned physical_ep) { return (kIsoMask >> physical_ep) & 1u; }

bool UsbDevice::modelled(std::uint32_t offset) {
    return (offset >= kDevIntSt && offset <= kMaxPSize && offset % 4 == 0) || offset == kClkCtrl || offset == kClkSt;
}

std::uint32_t UsbDevice::peek(std::uint32_t offset) const {
    switch (offset) {
    case kDevIntSt: return dev_int_st_;
    case kDevIntEn: return dev_int_en_;
    case kCmdData: return cmd_data_;
    case kRxData: {
        const Endpoint& ep = eps_[2 * ((ctrl_ >> 2) & 0xFu)];
        std::uint32_t word = 0;
        for (unsigned i = 0; i < 4 && rx_pos_ + i < ep.data.size(); ++i)
            word |= static_cast<std::uint32_t>(ep.data[rx_pos_ + i]) << (8 * i);
        return word;
    }
    case kRxPLen: {
        if (!(ctrl_ & kRdEn)) return 0;
        const unsigned phys = 2 * ((ctrl_ >> 2) & 0xFu);
        const Endpoint& ep = eps_[phys];
        // RD_EN fetches the packet length and sets PKT_RDY (11.14.2). An
        // isochronous endpoint always has this frame's packet, empty and not valid
        // (DV clear) if none arrived; others hold one only after receiving it.
        if (iso(phys)) {
            const std::size_t n = ep.full ? ep.data.size() : 0;
            return static_cast<std::uint32_t>(n) | (n != 0 ? kPktDv : 0u) | kPktRdy;
        }
        if (!ep.full) return 0;
        return static_cast<std::uint32_t>(ep.data.size()) | kPktDv | kPktRdy;
    }
    case kCtrl: return ctrl_;
    case kEpIntSt: return ep_int_st_;
    case kEpIntEn: return ep_int_en_;
    case kReEp: return re_ep_;
    case kMaxPSize: return eps_[ep_ind_ & 31u].max_packet;
    case kClkCtrl: return clk_ctrl_;
    case kClkSt: return clk_ctrl_;  // clocks run as soon as they are requested
    default: return 0;             // write-only registers
    }
}

void UsbDevice::read_side_effects(std::uint32_t offset) {
    if (offset == kRxData && (ctrl_ & kRdEn)) rx_pos_ += 4;
}

void UsbDevice::write(std::uint32_t offset, std::uint32_t value) {
    switch (offset) {
    case kDevIntEn: dev_int_en_ = value; break;
    case kDevIntClr: dev_int_st_ &= ~value; break;
    case kDevIntSet: dev_int_st_ |= value; break;
    case kCmdCode: command(value); break;
    case kTxData:
        for (unsigned i = 0; i < 4 && tx_.size() < tx_len_; ++i) tx_.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
        break;
    case kTxPLen:
        tx_len_ = value & 0x3FFu;
        tx_.clear();
        break;
    case kCtrl: {
        const std::uint32_t old = ctrl_;
        ctrl_ = value & 0x3Fu;
        if (ctrl_ & kRdEn) rx_pos_ = 0;
        if (ctrl_ & kWrEn) tx_.clear();
        // An isochronous packet is gone once read; others wait for Clear Buffer.
        if ((old & kRdEn) && !(ctrl_ & kRdEn)) {
            const unsigned phys = 2 * ((old >> 2) & 0xFu);
            if (iso(phys)) eps_[phys].full = false;
        }
        break;
    }
    case kDevIntPri: break;
    case kEpIntEn: ep_int_en_ = value; break;
    case kEpIntClr:  // each bit runs Select Endpoint/Clear Interrupt (11.10.3.3)
        dev_int_st_ &= ~kUsbCdFull;
        for (unsigned phys = 0; phys < 32; ++phys) {
            if (!((value >> phys) & 1u)) continue;
            ep_int_st_ &= ~(1u << phys);
            cmd_data_ = select_status(phys);
            if (phys == 0) eps_[0].setup = eps_[0].overwritten = false;
        }
        dev_int_st_ |= kUsbCdFull;
        break;
    case kEpIntSet:
        for (unsigned phys = 0; phys < 32; ++phys)
            if ((value >> phys) & 1u) ep_interrupt(phys);
        break;
    case kEpIntPri: ep_int_pri_ = value; break;
    case kReEp:
        re_ep_ = value;
        dev_int_st_ |= kUsbEpRealized;
        break;
    case kEpInd: ep_ind_ = value & 31u; break;
    case kMaxPSize:
        eps_[ep_ind_].max_packet = static_cast<std::uint16_t>(value & 0x3FFu);
        dev_int_st_ |= kUsbEpRealized;
        break;
    case kClkCtrl: clk_ctrl_ = value; break;
    default: break;  // read-only
    }
}

bool UsbDevice::interrupt() const { return (dev_int_st_ & dev_int_en_) != 0; }

// USBCmdCode: bits 15:8 phase (5 command, 1 write, 2 read), bits 23:16 code or data.
void UsbDevice::command(std::uint32_t value) {
    const auto phase = static_cast<std::uint8_t>(value >> 8);
    const auto code = static_cast<std::uint8_t>(value >> 16);
    switch (phase) {
    case 0x05:
        pending_cmd_ = code;
        has_pending_cmd_ = true;
        if (code < 0x20) selected_ = code;
        else if (code >= 0x40 && code < 0x60) selected_ = code - 0x40u;
        else if (code == 0xF2 && !iso(selected_)) eps_[selected_].full = false;  // Clear Buffer
        else if (code == 0xFA) {                                               // Validate Buffer
            eps_[selected_].data = tx_;
            eps_[selected_].full = true;
        } else if (code == 0xF5 || code == 0xFD) frame_read_ = 0;  // two-byte reads start over
        dev_int_st_ |= kUsbCcEmpty;
        break;
    case 0x01:
        if (has_pending_cmd_) command_write(code);
        dev_int_st_ |= kUsbCcEmpty;
        break;
    case 0x02:
        dev_int_st_ &= ~kUsbCdFull;
        cmd_data_ = has_pending_cmd_ ? command_read() : 0;
        dev_int_st_ |= kUsbCdFull;
        break;
    default: break;
    }
}

void UsbDevice::command_write(std::uint8_t data) {
    const std::uint8_t c = pending_cmd_;
    if (c == 0xD0) address_ = data;                     // Set Address (DEV_EN | address)
    else if (c == 0xD8) configured_ = (data & 1u) != 0;  // Configure Device
    else if (c == 0xFE)                                  // Set Device Status: CON, SUS
        dev_status_ = static_cast<std::uint8_t>((dev_status_ & ~(kCon | kSus)) | (data & (kCon | kSus)));
    else if (c >= 0x40 && c < 0x60) {  // Set Endpoint Status: ST, DA; re-initializes the endpoint
        Endpoint& ep = eps_[c - 0x40u];
        ep.stalled = (data & 1u) != 0;
        ep.disabled = (data & 0x20u) != 0;
        ep.full = false;
        ep.data.clear();
    }
    // Set Mode (0xF3) and anything else: accepted, no modelled effect.
}

std::uint8_t UsbDevice::command_read() {
    const std::uint8_t c = pending_cmd_;
    if (c == 0xF5) return static_cast<std::uint8_t>(frame_read_++ == 0 ? frame_ & 0xFFu : frame_ >> 8);
    if (c == 0xFD) return static_cast<std::uint8_t>(frame_read_++ == 0 ? 0x0F : 0xA5);  // test register 0xA50F
    if (c == 0xFE) {                                                                   // Get Device Status
        const std::uint8_t s = dev_status_;
        dev_status_ &= static_cast<std::uint8_t>(~(kConCh | kSusCh | kRst));
        return s;
    }
    if (c < 0x20) return select_status(c);
    if (c >= 0x40 && c < 0x60) {  // Select Endpoint/Clear Interrupt
        const unsigned phys = c - 0x40u;
        const std::uint8_t s = select_status(phys);
        ep_int_st_ &= ~(1u << phys);
        if (phys == 0) eps_[0].setup = eps_[0].overwritten = false;
        return s;
    }
    if (c == 0xF2) return eps_[selected_].overwritten ? 1 : 0;
    return 0;  // error code / error status: no errors
}

// Select Endpoint data (table 248): FE, ST, STP, PO, B_1_FULL.
std::uint8_t UsbDevice::select_status(unsigned phys) const {
    const Endpoint& ep = eps_[phys];
    return static_cast<std::uint8_t>((ep.full ? 1u : 0u) | (ep.stalled ? 2u : 0u) | (ep.setup ? 4u : 0u) |
                                     (ep.overwritten ? 8u : 0u) | (ep.full ? 0x20u : 0u));
}

void UsbDevice::ep_interrupt(unsigned phys) {
    ep_int_st_ |= 1u << phys;
    if (ep_int_en_ & (1u << phys)) dev_int_st_ |= kUsbEpSlowInt;
}

void UsbDevice::dev_status_changed(std::uint8_t change) {
    dev_status_ |= change;
    dev_int_st_ |= kUsbDevStatInt;
}

// ---- bus side ----

void UsbDevice::cable(bool attached) {
    if (attached == attached_) return;
    attached_ = attached;
    dev_status_changed(kConCh);
}

void UsbDevice::bus_reset() {
    address_ = 0;
    configured_ = false;
    for (Endpoint& ep : eps_) {
        ep.full = ep.setup = ep.overwritten = ep.stalled = false;
        ep.data.clear();
    }
    iso_next_ = {};
    dev_status_changed(kRst);
}

void UsbDevice::start_of_frame() {
    frame_ = (frame_ + 1) & 0x7FFu;
    for (unsigned phys = 0; phys < 32; phys += 2) {  // isochronous OUT: this frame's packet, or none
        if (!iso(phys) || !realized(phys)) continue;
        eps_[phys].data = iso_next_[phys].value_or(std::vector<std::uint8_t>{});
        eps_[phys].full = true;
        iso_next_[phys].reset();
    }
    dev_int_st_ |= kUsbFrameInt;
}

void UsbDevice::setup(const std::array<std::uint8_t, 8>& packet) {
    Endpoint& out = eps_[0];
    if (out.full) out.overwritten = true;
    out.data.assign(packet.begin(), packet.end());
    out.full = out.setup = true;
    out.stalled = eps_[1].stalled = false;  // a SETUP unstalls both control endpoints
    eps_[1].full = false;                   // and invalidates a validated IN buffer
    ep_interrupt(0);
}

std::optional<std::vector<std::uint8_t>> UsbDevice::in(unsigned ep) {
    const unsigned phys = 2 * ep + 1;
    Endpoint& e = eps_.at(phys);
    if (e.stalled || !e.full || !realized(phys)) return std::nullopt;
    std::vector<std::uint8_t> data = std::move(e.data);
    e.data.clear();
    e.full = false;
    ep_interrupt(phys);
    return data;
}

bool UsbDevice::out(unsigned ep, const std::vector<std::uint8_t>& data) {
    const unsigned phys = 2 * ep;
    Endpoint& e = eps_.at(phys);
    if (e.stalled || e.full || !realized(phys)) return false;
    e.data = data;
    e.full = true;
    e.setup = false;
    ep_interrupt(phys);
    return true;
}

void UsbDevice::iso_out(unsigned ep, const std::vector<std::uint8_t>& data) { iso_next_.at(2 * ep) = data; }

}  // namespace latasim::lpc17xx
