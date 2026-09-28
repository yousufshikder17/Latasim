/* E12: SysTick register semantics in the LPC1768 simulator, exercised by real
 * firmware (no interrupt). Each r[] slot records one register read; the INI prints
 * them all at the end. Reads a few instructions apart differ by a few clocks. */
#include "LPC17xx.h"

volatile uint32_t r[10];
volatile unsigned int g_id;

__attribute__((noinline)) void finished(void) { g_id = 0xFF; }

int main(void) {
    SysTick->CTRL = 0;
    SysTick->LOAD = 999;
    SysTick->VAL = 0;
    SysTick->CTRL = 5;                        /* ENABLE | CLKSOURCE, no TICKINT */
    r[0] = SysTick->VAL;                      /* just after enabling */
    while (!(SysTick->CTRL & (1UL << 16))) {} /* wait for COUNTFLAG; this read clears it */
    r[1] = SysTick->CTRL;                     /* cleared by the read that saw it? */
    while (SysTick->VAL < 900) {}             /* wait for the reload... */
    while (SysTick->VAL > 10) {}              /* ...and the next count toward 0 */
    while (SysTick->VAL <= 10 && SysTick->VAL != 0) {}
    r[2] = SysTick->CTRL;                     /* set again? */
    r[3] = SysTick->CTRL;                     /* and cleared by r[2]'s read? */
    SysTick->VAL = 123;                       /* any write clears VAL and COUNTFLAG */
    r[4] = SysTick->VAL;
    SysTick->CTRL = 4;                        /* stop: VAL holds */
    r[5] = SysTick->VAL;
    SysTick->LOAD = 500;
    SysTick->CTRL = 5;                        /* re-enable with VAL != 0 */
    r[6] = SysTick->VAL;                      /* ~500 if ENABLE loads RELOAD, else r[5]-ish */
    r[7] = SysTick->CALIB;
    finished();
    for (;;) {}
}
