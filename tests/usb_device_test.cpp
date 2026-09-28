// The LPC1768 USB device controller model (lpc17xx/usb_device.hpp) through its
// registers, as a driver uses them (UM10360 chapter 11), and through its bus side.
#include "lpc17xx/lpc1768.hpp"

#include <gtest/gtest.h>

#include <array>
#include <vector>

using namespace latasim;
using namespace latasim::lpc17xx;

namespace {

constexpr std::uint32_t kDevIntSt = 0x5000C200, kDevIntEn = 0x5000C204, kDevIntClr = 0x5000C208;
constexpr std::uint32_t kCmdCode = 0x5000C210, kCmdData = 0x5000C214;
constexpr std::uint32_t kRxData = 0x5000C218, kTxData = 0x5000C21C, kRxPLen = 0x5000C220, kTxPLen = 0x5000C224;
constexpr std::uint32_t kCtrl = 0x5000C228, kEpIntSt = 0x5000C230, kEpIntEn = 0x5000C234, kEpIntClr = 0x5000C238;
constexpr std::uint32_t kReEp = 0x5000C244, kEpInd = 0x5000C248, kMaxPSize = 0x5000C24C;

// SIE commands as USBD_LPC17xx.c issues them.
void sie_command(Lpc1768& m, std::uint8_t code) { m.write32(kCmdCode, 0x0500u | (code << 16)); }
void sie_write(Lpc1768& m, std::uint8_t code, std::uint8_t data) {
    sie_command(m, code);
    m.write32(kCmdCode, 0x0100u | (data << 16));
}
std::uint8_t sie_read(Lpc1768& m, std::uint8_t code) {
    sie_command(m, code);
    m.write32(kDevIntClr, kUsbCdFull);
    m.write32(kCmdCode, 0x0200u | (code << 16));
    EXPECT_TRUE(m.read32(kDevIntSt) & kUsbCdFull);
    return static_cast<std::uint8_t>(m.read32(kCmdData));
}

std::vector<std::uint8_t> read_packet(Lpc1768& m, unsigned ep) {
    m.write32(kCtrl, (ep << 2) | 1u);
    const std::uint32_t len = m.read32(kRxPLen);
    EXPECT_TRUE(len & (1u << 11)) << "PKT_RDY";
    std::vector<std::uint8_t> out;
    for (std::uint32_t i = 0; i < (len & 0x3FFu); i += 4) {
        const std::uint32_t w = m.read32(kRxData);
        for (unsigned b = 0; b < 4 && i + b < (len & 0x3FFu); ++b) out.push_back(static_cast<std::uint8_t>(w >> (8 * b)));
    }
    m.write32(kCtrl, 0);
    return out;
}

void write_packet(Lpc1768& m, unsigned ep, const std::vector<std::uint8_t>& data) {
    m.write32(kCtrl, (ep << 2) | 2u);
    m.write32(kTxPLen, static_cast<std::uint32_t>(data.size()));
    for (std::size_t i = 0; i < data.size(); i += 4) {
        std::uint32_t w = 0;
        for (unsigned b = 0; b < 4 && i + b < data.size(); ++b) w |= std::uint32_t{data[i + b]} << (8 * b);
        m.write32(kTxData, w);
    }
    m.write32(kCtrl, 0);
    sie_command(m, static_cast<std::uint8_t>(2 * ep + 1));  // select the IN endpoint
    sie_command(m, 0xFA);                                   // Validate Buffer
}

struct CountingHost : usb::HostPort {
    unsigned frames = 0;
    void frame(usb::Bus&) override { ++frames; }
};

}  // namespace

TEST(UsbDevice, SieCommandsCompleteAtOnce) {
    Lpc1768 m;
    EXPECT_EQ(m.read32(kDevIntSt) & kUsbCcEmpty, kUsbCcEmpty) << "reset value";
    m.write32(kDevIntClr, kUsbCcEmpty);
    sie_write(m, 0xFE, 1);  // Set Device Status: CON
    EXPECT_EQ(m.read32(kDevIntSt) & kUsbCcEmpty, kUsbCcEmpty);
    EXPECT_TRUE(m.usb().pull_up());
    EXPECT_EQ(sie_read(m, 0xFE), 1u);  // Get Device Status
    sie_write(m, 0xD0, 0x80 | 5);      // Set Address
    EXPECT_EQ(m.usb().address(), 5u);
    sie_write(m, 0xD8, 1);  // Configure Device
    EXPECT_TRUE(m.usb().configured());
    sie_command(m, 0xFD);
    EXPECT_EQ(sie_read(m, 0xFD), 0x0Fu) << "test register 0xA50F, low byte first";
}

TEST(UsbDevice, RealizingAnEndpointSignalsCompletion) {
    Lpc1768 m;
    m.write32(kDevIntClr, 0xFFFFFFFFu);
    m.write32(kReEp, m.read32(kReEp) | (1u << 6));
    EXPECT_EQ(m.read32(kDevIntSt) & kUsbEpRealized, kUsbEpRealized);
    m.write32(kEpInd, 6);
    m.write32(kMaxPSize, 64);
    EXPECT_EQ(m.read32(kMaxPSize), 64u);
    EXPECT_TRUE(m.usb().realized(6));
}

TEST(UsbDevice, CableAndBusResetRaiseDeviceStatus) {
    Lpc1768 m;
    m.write32(kDevIntEn, kUsbDevStatInt);
    sie_write(m, 0xFE, 1);
    m.usb().cable(true);
    EXPECT_TRUE(m.read32(kDevIntSt) & kUsbDevStatInt);
    EXPECT_EQ(sie_read(m, 0xFE), 0x03u) << "CON, CON_CH";
    EXPECT_EQ(sie_read(m, 0xFE), 0x01u) << "change bits clear on read";
    m.usb().bus_reset();
    EXPECT_EQ(sie_read(m, 0xFE) & 0x10u, 0x10u) << "RST";
}

TEST(UsbDevice, SetupPacketsArriveOnEndpointZero) {
    Lpc1768 m;
    m.write32(kEpIntEn, 3);
    m.usb().setup({0x80, 6, 0, 1, 0, 0, 18, 0});
    EXPECT_EQ(m.read32(kEpIntSt), 1u);
    EXPECT_TRUE(m.read32(kDevIntSt) & kUsbEpSlowInt);
    m.write32(kEpIntClr, 1);  // Select Endpoint/Clear Interrupt
    EXPECT_EQ(m.read32(kCmdData) & 0x25u, 0x25u) << "FE, STP, B_1_FULL";
    EXPECT_EQ(m.read32(kEpIntSt), 0u);
    EXPECT_EQ(read_packet(m, 0), (std::vector<std::uint8_t>{0x80, 6, 0, 1, 0, 0, 18, 0}));
    sie_command(m, 0);
    sie_command(m, 0xF2);  // Clear Buffer
    EXPECT_EQ(sie_read(m, 0) & 1u, 0u) << "empty";
}

TEST(UsbDevice, ValidatedInPacketsGoToTheHost) {
    Lpc1768 m;
    m.write32(kEpIntEn, 3);
    EXPECT_FALSE(m.usb().in(0)) << "nothing validated: NAK";
    write_packet(m, 0, {1, 2, 3, 4, 5});
    const auto got = m.usb().in(0);
    ASSERT_TRUE(got);
    EXPECT_EQ(*got, (std::vector<std::uint8_t>{1, 2, 3, 4, 5}));
    EXPECT_EQ(m.read32(kEpIntSt), 2u) << "IN endpoint interrupt";
    write_packet(m, 0, {});
    EXPECT_EQ(m.usb().in(0)->size(), 0u) << "a zero-length packet";
}

TEST(UsbDevice, StalledControlEndpointIsSeenByTheHostAndClearedBySetup) {
    Lpc1768 m;
    sie_write(m, 0x41, 1);  // Set Endpoint Status: EP0 IN stalled
    EXPECT_FALSE(m.usb().in(0));
    EXPECT_TRUE(m.usb().control_stalled());
    m.usb().setup({});
    EXPECT_FALSE(m.usb().control_stalled());
}

TEST(UsbDevice, IsochronousPacketsBecomeReadableAtTheNextFrame) {
    Lpc1768 m;
    m.write32(kReEp, 3u | (1u << 6));
    m.usb().iso_out(3, {0x10, 0x20, 0x30, 0x40});
    m.write32(kCtrl, (3u << 2) | 1u);
    EXPECT_EQ(m.read32(kRxPLen), 1u << 11) << "not before the frame ends: an empty packet (11.14.2)";
    m.write32(kCtrl, 0);
    m.usb().start_of_frame();
    EXPECT_TRUE(m.read32(kDevIntSt) & kUsbFrameInt);
    EXPECT_EQ(read_packet(m, 3), (std::vector<std::uint8_t>{0x10, 0x20, 0x30, 0x40}));
    m.usb().start_of_frame();  // no packet this frame: an empty, invalid one
    m.write32(kCtrl, (3u << 2) | 1u);
    EXPECT_EQ(m.read32(kRxPLen), 1u << 11) << "PKT_RDY, length 0, DV clear";
}

TEST(UsbDevice, AnAttachedHostGetsAFrameEveryMillisecond) {
    Lpc1768 m;
    CountingHost host;
    m.attach_usb_host(&host);
    EXPECT_TRUE(m.usb().attached());
    m.advance_cycles(10 * kUsbFrameCycles + 5);
    EXPECT_EQ(host.frames, 10u);
    m.attach_usb_host(nullptr);
    m.advance_cycles(10 * kUsbFrameCycles);
    EXPECT_EQ(host.frames, 10u);
    EXPECT_FALSE(m.usb().attached());
}

TEST(UsbDevice, InterruptLineIsIrq24) {
    Lpc1768 m;
    bool taken = false;
    m.bind_handler(kUsbIrq, [&] {
        taken = true;
        m.write32(kDevIntClr, kUsbFrameInt);
    });
    m.write32(0xE000E100, 1u << 24);  // ISER0
    m.write32(kDevIntEn, kUsbFrameInt);
    m.write32(kReEp, 3);
    m.usb().start_of_frame();
    m.advance_cycles(1);
    EXPECT_TRUE(taken);
}

TEST(Dac, ValueDrivesTheOutput) {
    Lpc1768 m;
    std::vector<std::uint32_t> out;
    m.on_dac_output([&](std::uint32_t v) { out.push_back(v); });
    m.write32(0x4008C000, 0x3FFu << 6);
    m.write32(0x4008C000, 512u << 6 | 0x3Fu);  // low bits are not part of VALUE
    EXPECT_EQ(out, (std::vector<std::uint32_t>{1023, 512}));
    EXPECT_EQ(m.read32(0x4008C000), 512u << 6);
    EXPECT_EQ(m.dac_value(), 512u);
}
