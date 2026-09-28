/* A representative USB Audio Class 1.0 speaker for the MCB1700 (docs/phase5/usb-audio.md).
 *
 * Keil's own USB audio example sits on MDK's USB device middleware, which ships
 * only as ARM object code and cannot run on the host. This firmware does the same
 * job directly on Keil's CMSIS USB device driver (USBD_LPC17xx.c, unchanged) and
 * the board's DAC driver (DAC_MCB1700.c, unchanged), with the example's format:
 * 32 kHz, mono, 16-bit PCM on isochronous OUT endpoint 3 (64-byte packets), a
 * TIMER0 interrupt at the sample rate writing the DAC, and the example's volume
 * scaling. Volume comes from the application (the board's potentiometer), not from
 * host requests: audio class requests are stalled.
 *
 * Interrupt handlers to bind: USB_IRQHandler (OTG_LPC17xx.c) to the USB IRQ,
 * usb_speaker_timer_irq to TIMER0.
 */
#ifndef LATASIM_FIRMWARE_USB_SPEAKER_H
#define LATASIM_FIRMWARE_USB_SPEAKER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define USB_SPEAKER_RATE 32000u
#define USB_SPEAKER_BUFFER 1024u /* samples (the example's 2048-byte buffer) */

typedef struct {
    uint32_t connected;  /* DeviceConnect called and not disconnected */
    uint32_t configured; /* the host set configuration 1 */
    uint32_t streaming;  /* the host selected the streaming alternate setting */
    uint32_t playing;    /* the playback timer is running */
    uint32_t packets;
    uint32_t samples_received;
    uint32_t samples_played;
    uint32_t underruns;  /* playback ticks that found the buffer empty */
    uint32_t overruns;   /* received samples dropped because the buffer was full */
    uint32_t buffer_level;
    uint32_t volume;     /* 0-256 */
} usb_speaker_status_t;

extern volatile usb_speaker_status_t usb_speaker;

/* DAC and playback timer set-up, then the USB device: initialize, power, connect. */
void usb_speaker_start(void);
/* Disconnect from the bus, power the controller down, stop playback. */
void usb_speaker_stop(void);
void usb_speaker_set_volume(uint32_t volume);
/* Playback: one sample per interrupt, at USB_SPEAKER_RATE. */
void usb_speaker_timer_irq(void);
/* Host statics back to their initial state. */
void usb_speaker_reset(void);

void USB_IRQHandler(void);

#ifdef __cplusplus
}
#endif

#endif /* LATASIM_FIRMWARE_USB_SPEAKER_H */
