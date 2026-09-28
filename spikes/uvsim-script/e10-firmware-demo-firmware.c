/* E10: the Phase 2 firmware-demo scenario as real firmware in the LPC1768
 * simulator: Keil's MCB1700 LED, joystick and button drivers initialise the board,
 * LED_On(0), read a joystick UP press and an INT0 press injected by the INI at
 * checkpoints, and show both with LED_SetOut. The LED driver is the project's own
 * board-support component; the joystick and button drivers are compiled into this
 * file from the pack (the project's include path points at
 * Boards/Keil/MCB1700/Common and MDK-Middleware's Board directory). */
#include "LPC17xx.h"
#include "Board_LED.h"
#include "Board_Joystick.h"
#include "Board_Buttons.h"
#include "Joystick_MCB1700.c"
#include "Buttons_MCB1700.c"

volatile unsigned int g_id, g_joystick, g_buttons;

__attribute__((noinline)) void checkpoint(unsigned int id) { g_id = id; }
__attribute__((noinline)) void finished(void) { g_id = 0xFF; }

int main(void) {
    LED_Initialize();
    Joystick_Initialize();
    Buttons_Initialize();
    checkpoint(1);
    LED_On(0);
    checkpoint(2);                      /* INI: press joystick UP (P1.23 low) */
    g_joystick = Joystick_GetState();
    checkpoint(3);                      /* INI: press INT0 (P2.10 low) */
    g_buttons = Buttons_GetState();
    checkpoint(4);
    LED_SetOut(g_joystick | g_buttons);
    checkpoint(5);                      /* INI: release both */
    finished();
    for (;;) {}
}
