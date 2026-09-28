/* A representative media-center application for the MCB1700 (docs/phase5/overview.md):
 * CMSIS-RTOS v1 firmware on Keil's board drivers (GLCD, joystick, LEDs, ADC) and
 * the USB speaker (usb_speaker.h).
 *
 * A menu on the GLCD, moved through with the joystick (LED n shows item n), opens:
 *   - a photo gallery: generated RGB565 pictures, LEFT/RIGHT to browse;
 *   - an audio player: connects as a USB speaker, the potentiometer sets the
 *     volume, the screen shows the volume and stream state;
 *   - a paddle game: LEFT/RIGHT move the paddle, the score counts returns.
 * CENTER selects in the menu and returns to it from each item; leaving the audio
 * player disconnects from USB.
 *
 * Interrupt handlers to bind besides the kernel's SysTick: ADC_IRQHandler (ADC),
 * USB_IRQHandler (USB), usb_speaker_timer_irq (TIMER0).
 */
#ifndef LATASIM_FIRMWARE_MEDIA_CENTER_H
#define LATASIM_FIRMWARE_MEDIA_CENTER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum media_screen { MEDIA_MENU, MEDIA_PHOTOS, MEDIA_AUDIO, MEDIA_GAME };

typedef struct {
    uint32_t screen;     /* enum media_screen */
    uint32_t selection;  /* menu item 0-2 */
    uint32_t photo;      /* picture shown, 0-2 */
    uint32_t score;      /* game */
    uint32_t misses;
    uint32_t ball_x, ball_y, paddle_x;
    uint32_t volume;     /* last volume set from the potentiometer, 0-255 */
    uint32_t redraws;    /* screens drawn */
} media_status_t;

extern volatile media_status_t media;

#define MEDIA_ITEMS 3
#define MEDIA_PHOTOS_COUNT 3
#define MEDIA_POLL_MS 20

void media_main(void);   /* the application's main(), run as RTX's main thread */
void media_reset(void);  /* host statics */

void ADC_IRQHandler(void);

#ifdef __cplusplus
}
#endif

#endif /* LATASIM_FIRMWARE_MEDIA_CENTER_H */
