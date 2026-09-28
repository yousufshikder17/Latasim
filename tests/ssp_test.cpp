// SSP1 (lpc17xx/ssp.hpp): its registers through the LPC1768 memory map, and the
// MCB1700's GLCD on its bus driven by register-level firmware (host/ssp_glcd.cpp).
#include "boards/mcb1700/board.hpp"
#include "host/binding.hpp"
#include "host/ssp_glcd.h"
#include "lpc17xx/lpc1768.hpp"
#include "trace/trace.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

using latasim::TraceKind;
using latasim::lpc17xx::BusFault;
using latasim::lpc17xx::Lpc1768;
using latasim::lpc17xx::NotModelled;

namespace {

constexpr std::uint32_t kCr0 = 0x40030000, kCr1 = 0x40030004, kDr = 0x40030008, kSr = 0x4003000C,
                        kCpsr = 0x40030010, kImsc = 0x40030014, kDmacr = 0x40030024;

// The legacy GLCD driver's configuration: 8-bit SPI, CPOL = CPHA = 1, master, enabled.
void configure(Lpc1768& mcu) {
    mcu.write32(kCr0, 0x01C7);
    mcu.write32(kCpsr, 0x02);
    mcu.write32(kCr1, 0x02);
}

}  // namespace

TEST(Ssp1, ResetValues) {
    Lpc1768 mcu;
    EXPECT_EQ(mcu.read32(kCr0), 0u);
    EXPECT_EQ(mcu.read32(kCr1), 0u);
    EXPECT_EQ(mcu.read32(kCpsr), 0u);
    EXPECT_EQ(mcu.read32(kSr), 0x03u) << "TFE and TNF (UM10360 Table 373)";
    EXPECT_EQ(mcu.read32(kDr), 0u) << "empty receive FIFO";
}

TEST(Ssp1, ConfigurationIsStored) {
    Lpc1768 mcu;
    configure(mcu);
    EXPECT_EQ(mcu.read32(kCr0), 0x01C7u);
    EXPECT_EQ(mcu.read32(kCr1), 0x02u);
    EXPECT_EQ(mcu.read32(kCpsr), 0x02u);
    mcu.write32(kCpsr, 0xFFFFFFFF);
    EXPECT_EQ(mcu.read32(kCpsr), 0xFEu) << "CPSDVSR: bit 0 reads 0";
    mcu.write32(kCr1, 0xFFFFFFF2);
    EXPECT_EQ(mcu.read32(kCr1), 0x02u) << "CR1 has four bits";
}

TEST(Ssp1, DrWriteSendsAFrameAndQueuesTheReply) {
    Lpc1768 mcu;
    std::vector<std::uint8_t> sent;
    mcu.attach_ssp1([&](std::uint8_t mosi) {
        sent.push_back(mosi);
        return static_cast<std::uint8_t>(~mosi);
    });
    configure(mcu);
    mcu.write32(kDr, 0x1A5);  // 8-bit frames: the low byte
    ASSERT_EQ(sent, std::vector<std::uint8_t>{0xA5});
    EXPECT_EQ(mcu.read32(kSr), 0x07u) << "TFE, TNF, RNE: sent at once, reply waiting";
    EXPECT_EQ(mcu.read32(kDr), 0x5Au);
    EXPECT_EQ(mcu.read32(kSr), 0x03u) << "the read emptied the receive FIFO";
    EXPECT_EQ(mcu.read32(kDr), 0u);
}

TEST(Ssp1, ReceiveFifoHoldsEightFramesInOrder) {
    Lpc1768 mcu;
    mcu.attach_ssp1([](std::uint8_t mosi) { return mosi; });
    configure(mcu);
    for (std::uint32_t i = 0; i < 8; ++i) mcu.write32(kDr, i);
    EXPECT_EQ(mcu.read32(kSr), 0x0Fu) << "RFF";
    EXPECT_THROW(mcu.write32(kDr, 8), NotModelled) << "overrun: which frame survives is not specified";
    for (std::uint32_t i = 0; i < 8; ++i) EXPECT_EQ(mcu.read32(kDr), i);
}

TEST(Ssp1, NoPeerRepliesWithOnes) {
    Lpc1768 mcu;
    configure(mcu);
    mcu.write32(kDr, 0x12);
    EXPECT_EQ(mcu.read32(kDr), 0xFFu);
}

TEST(Ssp1, UnmodelledUseIsReported) {
    Lpc1768 mcu;
    EXPECT_THROW(mcu.write32(kDr, 0x12), NotModelled) << "disabled: the frame would wait in the transmit FIFO";
    configure(mcu);
    mcu.write32(kCr0, 0x01CF);  // 16-bit frames
    EXPECT_THROW(mcu.write32(kDr, 0x12), NotModelled);
    mcu.write32(kCr0, 0x01D7);  // TI frame format
    EXPECT_THROW(mcu.write32(kDr, 0x12), NotModelled);
    mcu.write32(kCr0, 0x01C7);
    mcu.write32(kCr1, 0x06);  // slave
    EXPECT_THROW(mcu.write32(kDr, 0x12), NotModelled);
    mcu.write32(kCr1, 0x03);  // loopback
    EXPECT_THROW(mcu.write32(kDr, 0x12), NotModelled);
    EXPECT_THROW(mcu.write32(kImsc, 0), NotModelled);
    EXPECT_THROW(mcu.read32(kDmacr), NotModelled);
    EXPECT_THROW(mcu.write32(kSr, 0), BusFault) << "read-only";
    EXPECT_THROW(mcu.read8(kDr), BusFault) << "word registers";
    EXPECT_THROW(mcu.read32(0x40088008), BusFault) << "SSP0 is not modelled";
}

TEST(Ssp1, AccessesAreTracedByName) {
    Lpc1768 mcu;
    configure(mcu);
    mcu.write32(kDr, 0x70);
    mcu.read32(kSr);
    const auto& events = mcu.trace().events();
    ASSERT_EQ(events.size(), 5u);
    EXPECT_EQ(latasim::to_string(events[3]), "#4    t=0          write32 SSP1DR    0x00000070");
    EXPECT_EQ(latasim::to_string(events[4]), "#5    t=0          read32  SSP1SR    0x00000007");
}

// The MCB1700 wires SSP1 to the GLCD: register-level firmware reads the
// controller's ID and draws, and the trace shows the SSP1 traffic, then the
// controller writes it caused.
TEST(Ssp1, RegisterLevelFirmwareDrivesTheGlcd) {
    latasim::mcb1700::Board board;
    latasim::host::FirmwareBinding bind(board);
    ssp_glcd_init();
    EXPECT_EQ(ssp_glcd_read_reg(0x00), 0x9320u) << "ILI9320 ID through SSP1";
    ssp_glcd_write_reg(0x07, 0x0137);
    EXPECT_EQ(board.glcd().reg(0x07), 0x0137u);
    ssp_glcd_fill(10, 20, 3, 2, 0xF800);
    for (unsigned x = 10; x < 13; ++x)
        for (unsigned y = 20; y < 22; ++y) EXPECT_EQ(board.glcd().pixel(x, y), 0xF800u) << x << "," << y;
    EXPECT_EQ(board.glcd().pixel(13, 20), 0u);
    EXPECT_EQ(board.glcd().pixel(10, 22), 0u);
    EXPECT_EQ(board.glcd().bad_start_bytes(), 0u);

    const auto& events = board.mcu().trace().events();
    std::size_t display = 0;
    for (std::size_t i = 0; i < events.size(); ++i)
        if (events[i].kind == TraceKind::Display && events[i].address == 0x07) display = i;
    ASSERT_GT(display, 1u);
    const auto& last_byte = events[display - 1];
    EXPECT_TRUE(last_byte.kind == TraceKind::Write && last_byte.address == kDr && last_byte.value == 0x37)
        << "the last data byte, then the controller write: " << latasim::to_string(last_byte);
    std::size_t gram = 0;
    for (const auto& e : events) gram += e.kind == TraceKind::Display && e.address == 0x22 && e.value == 6;
    EXPECT_EQ(gram, 1u) << "one six-pixel GRAM burst";
}
