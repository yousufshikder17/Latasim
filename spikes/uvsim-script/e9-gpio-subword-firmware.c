/* E9: compiled byte/halfword stores and loads to LPC17xx Fast GPIO, run as normal
 * firmware in the LPC1768 simulator. The INI stops at checkpoint() and reads the
 * port-1 registers with 32-bit debugger reads (which E8 part 1 showed are 32-bit).
 * FIOSET reads back the output latch, so it shows lanes the store did not address.
 * Port 1 byte 3 (P1.24-31) is input and reads high, so a store that behaved as a
 * word read-modify-write of FIOPIN would copy 1s into byte 3 of the latch. */
#include "LPC17xx.h"

volatile unsigned int g_id;
volatile unsigned int g_load8, g_load16;

__attribute__((noinline)) void checkpoint(unsigned int id) { g_id = id; }
__attribute__((noinline)) void finished(void) { g_id = 0xFF; }

int main(void) {
    LPC_GPIO1->FIOMASK = 0;
    LPC_GPIO1->FIODIR = 0x00FFFFFF;      /* byte 3 input */
    LPC_GPIO1->FIOPIN = 0;               /* latch 0 */
    checkpoint(0);

    LPC_GPIO1->FIOPIN0 = 0xA5;           /* STRB */
    checkpoint(1);

    LPC_GPIO1->FIOPIN = 0;
    LPC_GPIO1->FIOPINL = 0x5AA5;         /* STRH */
    checkpoint(2);

    LPC_GPIO1->FIODIR = 0xFFFFFFFF;
    LPC_GPIO1->FIOPIN = 0;
    LPC_GPIO1->FIOSET1 = 0x01;           /* STRB: bit 8 only? */
    checkpoint(3);

    LPC_GPIO1->FIOSETH = 0x0001;         /* STRH: bit 16 only? */
    checkpoint(4);

    LPC_GPIO1->FIOPIN = 0xFFFFFFFF;
    LPC_GPIO1->FIOCLR2 = 0x01;           /* STRB: clear bit 16 only? */
    checkpoint(5);

    LPC_GPIO1->FIODIR3 = 0x00;           /* STRB: byte 3 input, others unchanged? */
    checkpoint(6);

    LPC_GPIO1->FIODIR = 0xFFFFFFFF;
    LPC_GPIO1->FIOPIN = 0;
    LPC_GPIO1->FIOMASK0 = 0x0F;          /* STRB */
    LPC_GPIO1->FIOPIN0 = 0xFF;           /* STRB under mask: latch 0xF0? */
    checkpoint(7);

    LPC_GPIO1->FIOMASK = 0;
    LPC_GPIO1->FIOPIN = 0x12345678;
    g_load8 = LPC_GPIO1->FIOPIN3;        /* LDRB */
    g_load16 = LPC_GPIO1->FIOPINH;       /* LDRH */
    checkpoint(8);

    /* PCONP: clear PCGPIO, then try a GPIO store. */
    LPC_GPIO1->FIOPIN = 0;
    LPC_SC->PCONP &= ~(1UL << 15);
    LPC_GPIO1->FIOSET = 0x00000100;
    checkpoint(9);
    LPC_SC->PCONP |= (1UL << 15);
    LPC_GPIO1->FIOSET = 0x00000200;
    checkpoint(10);

    finished();
    for (;;) {}
}
