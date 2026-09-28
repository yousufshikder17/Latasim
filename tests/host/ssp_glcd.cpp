// Representative register-level GLCD firmware, compiled against the host
// LPC17xx.h (see ssp_glcd.h).
#include "LPC17xx.h"

#include "host/ssp_glcd.h"

namespace {

constexpr uint32_t kCs = 1u << 6;    // P0.6
constexpr uint32_t kRne = 1u << 2;   // SSPnSR: receive FIFO not empty

uint8_t transfer(uint8_t byte) {
    LPC_SSP1->DR = byte;
    while (!(LPC_SSP1->SR & kRne)) {
    }
    return static_cast<uint8_t>(LPC_SSP1->DR);
}

// Start byte 0x70 | RS << 1 | RW, then 16-bit words, most significant byte first.
void write_word(uint8_t start, uint16_t word) {
    LPC_GPIO0->FIOCLR = kCs;
    transfer(start);
    transfer(static_cast<uint8_t>(word >> 8));
    transfer(static_cast<uint8_t>(word & 0xFF));
    LPC_GPIO0->FIOSET = kCs;
}

}  // namespace

void ssp_glcd_init(void) {
    LPC_SC->PCONP |= 1u << 10;             // PCSSP1
    LPC_GPIO0->FIODIR |= kCs;
    LPC_GPIO0->FIOSET = kCs;
    LPC_PINCON->PINSEL0 |= 0x000A8000u;    // P0.7 SCK1, P0.8 MISO1, P0.9 MOSI1
    LPC_SSP1->CR0 = 0x01C7;                // 8-bit SPI, CPOL = CPHA = 1, SCR = 1
    LPC_SSP1->CPSR = 0x02;
    LPC_SSP1->CR1 = 0x02;                  // enabled, master
}

void ssp_glcd_write_reg(uint8_t reg, uint16_t value) {
    write_word(0x70, reg);    // index
    write_word(0x72, value);  // data
}

uint16_t ssp_glcd_read_reg(uint8_t reg) {
    write_word(0x70, reg);
    LPC_GPIO0->FIOCLR = kCs;
    transfer(0x73);  // RS = 1, RW = 1
    transfer(0);     // dummy
    uint16_t value = static_cast<uint16_t>(transfer(0) << 8);
    value = static_cast<uint16_t>(value | transfer(0));
    LPC_GPIO0->FIOSET = kCs;
    return value;
}

void ssp_glcd_fill(unsigned x, unsigned y, unsigned w, unsigned h, uint16_t color) {
    ssp_glcd_write_reg(0x03, 0x1038);  // landscape entry mode
    ssp_glcd_write_reg(0x50, static_cast<uint16_t>(y));
    ssp_glcd_write_reg(0x51, static_cast<uint16_t>(y + h - 1));
    ssp_glcd_write_reg(0x52, static_cast<uint16_t>(x));
    ssp_glcd_write_reg(0x53, static_cast<uint16_t>(x + w - 1));
    ssp_glcd_write_reg(0x20, static_cast<uint16_t>(y));
    ssp_glcd_write_reg(0x21, static_cast<uint16_t>(x));
    write_word(0x70, 0x22);
    LPC_GPIO0->FIOCLR = kCs;
    transfer(0x72);
    for (unsigned i = 0; i < w * h; ++i) {
        transfer(static_cast<uint8_t>(color >> 8));
        transfer(static_cast<uint8_t>(color & 0xFF));
    }
    LPC_GPIO0->FIOSET = kCs;
}
