/* A repo-owned stand-in for external LPC1768 firmware (external_firmware_test.cpp).
 *
 * A plain bare-metal C program in the style of legacy Keil-era firmware, built by
 * latasim_add_host_firmware exactly as an external folder is. It uses the
 * constructs that path has to carry: device-header registers, SSP1 polling to the
 * GLCD with chip select on P0.6, a bit-band alias computed from a register's
 * address and kept in a pointer, a literal alias address, a __NOP delay loop, an
 * ITM fputc retarget with printf, and a final idle loop. The LATASIM_HOST block is
 * the documented source adaptation (docs/external-firmware.md). */
#include <stdio.h>

#include "LPC17xx.h"

/* printf over ITM stimulus port 0, as on the target. */
#define ITM_PORT8(n) (*((volatile unsigned char *)(0xE0000000 + 4 * (n))))
#define ITM_PORT32(n) (*((volatile unsigned long *)(0xE0000000 + 4 * (n))))
#define DEMCR (*((volatile unsigned long *)0xE000EDFC))

int fputc(int ch, FILE *f) {
    (void)f;
    if (DEMCR & 0x01000000UL) {
        while (ITM_PORT32(0) == 0) {
        }
        ITM_PORT8(0) = (unsigned char)ch;
    }
    return ch;
}

#ifdef LATASIM_HOST
#define REG(address) LATASIM_REG32(address)
typedef LATASIM_REG32_PTR reg_ptr;
#else
#define REG(address) (*((volatile unsigned long *)(address)))
typedef volatile unsigned long *reg_ptr;
#endif

#define BIT_BAND(reg, bit)                                                            \
    REG(((unsigned long)(reg) & 0xF0000000) | 0x02000000 | (((unsigned long)(reg) & 0x000FFFFF) << 5) | \
        ((bit) << 2))
#define LED0_ALIAS REG(0x233806F0) /* P1.28 in FIO1PIN's bit-band alias */

volatile unsigned long sample_passes;
static reg_ptr bit;

static void delay(volatile unsigned long count) {
    while (count--) {
        __NOP();
    }
}

static unsigned char spi(unsigned char byte) {
    LPC_SSP1->DR = byte;
    while (!(LPC_SSP1->SR & 0x04)) { /* RNE */
    }
    return (unsigned char)LPC_SSP1->DR;
}

static void glcd_word(unsigned char start, unsigned short word) {
    LPC_GPIO0->FIOCLR = 1UL << 6;
    spi(start);
    spi((unsigned char)(word >> 8));
    spi((unsigned char)(word & 0xFF));
    LPC_GPIO0->FIOSET = 1UL << 6;
}

static void glcd_reg(unsigned char reg, unsigned short value) {
    glcd_word(0x70, reg);
    glcd_word(0x72, value);
}

int main(void) {
    unsigned int i;

    LPC_GPIO1->FIODIR |= 1UL << 28;
    LPC_GPIO1->FIOCLR = 1UL << 28;

    /* A 4 x 2 red block at (8, 16) in landscape coordinates. */
    LPC_GPIO0->FIODIR |= 1UL << 6;
    LPC_GPIO0->FIOSET = 1UL << 6;
    LPC_SSP1->CR0 = 0x01C7;
    LPC_SSP1->CPSR = 0x02;
    LPC_SSP1->CR1 = 0x02;
    glcd_reg(0x03, 0x1038);
    glcd_reg(0x50, 16);
    glcd_reg(0x51, 17);
    glcd_reg(0x52, 8);
    glcd_reg(0x53, 11);
    glcd_reg(0x20, 16);
    glcd_reg(0x21, 8);
    glcd_word(0x70, 0x22);
    LPC_GPIO0->FIOCLR = 1UL << 6;
    spi(0x72);
    for (i = 0; i < 8; i++) {
        spi(0xF8);
        spi(0x00);
    }
    LPC_GPIO0->FIOSET = 1UL << 6;
    printf("sample firmware: display drawn\n");

    delay(1000);
    bit = &BIT_BAND(&LPC_GPIO1->FIOPIN, 28);
    *bit = 1;
    delay(1000);
    LED0_ALIAS = 0;

    while (1) {
        sample_passes++;
        __NOP();
    }
}
