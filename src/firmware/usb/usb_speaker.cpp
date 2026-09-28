// A representative USB Audio Class 1.0 speaker (usb_speaker.h), written as
// register-level MCB1700 firmware against the host LPC17xx.h (hence C++).
#include "usb_speaker.h"

#include <cstring>

#include "LPC17xx.h"

extern "C" {
#include "Board_DAC.h"
#include "Driver_USBD.h"
extern ARM_DRIVER_USBD Driver_USBD0;
}

volatile usb_speaker_status_t usb_speaker;

namespace {

ARM_DRIVER_USBD& usbd = Driver_USBD0;

constexpr std::uint8_t kIsoEndpoint = 0x03;
constexpr std::uint16_t kIsoPacket = 64;
constexpr std::uint8_t kStreamingInterface = 1;

// Device: USB 1.1, class in interfaces, 8-byte EP0, a test VID/PID (pid.codes 0x1209).
const std::uint8_t kDevice[18] = {18, 1, 0x10, 0x01, 0, 0, 0, 8, 0x09, 0x12, 0x51, 0x7A, 0x00, 0x01, 0, 0, 0, 1};

// Configuration: audio control interface (input terminal -> feature unit -> speaker),
// and an audio streaming interface whose alternate setting 1 has the isochronous
// OUT endpoint (USB Audio Class 1.0, Type I PCM).
const std::uint8_t kConfiguration[] = {
    9, 2, 109, 0, 2, 1, 0, 0x80, 50,               // configuration: 109 bytes, 2 interfaces, 100 mA
    9, 4, 0, 0, 0, 1, 1, 0, 0,                     // interface 0: audio control
    9, 0x24, 1, 0x00, 0x01, 39, 0, 1, 1,           // AC header, 1 streaming interface: 1
    12, 0x24, 2, 1, 0x01, 0x01, 0, 1, 0, 0, 0, 0,  // input terminal 1: USB streaming, mono
    9, 0x24, 6, 2, 1, 1, 0x03, 0x00, 0,            // feature unit 2: mute, volume
    9, 0x24, 3, 3, 0x01, 0x03, 0, 2, 0,            // output terminal 3: speaker
    9, 4, 1, 0, 0, 1, 2, 0, 0,                     // interface 1 alt 0: no bandwidth
    9, 4, 1, 1, 1, 1, 2, 0, 0,                     // interface 1 alt 1: streaming
    7, 0x24, 1, 1, 1, 0x01, 0x00,                  // AS general: terminal 1, PCM
    11, 0x24, 2, 1, 1, 2, 16, 1, 0x00, 0x7D, 0x00,  // Type I: mono, 2 bytes, 16 bits, 32000 Hz
    9, 5, kIsoEndpoint, 0x09, kIsoPacket, 0, 1, 0, 0,  // endpoint 3 OUT, isochronous adaptive
    7, 0x25, 1, 0, 0, 0, 0,                        // audio endpoint
};
static_assert(sizeof kConfiguration == 109, "configuration descriptor length");

std::uint8_t setup[8];
std::uint8_t ep0_data[8];
std::uint8_t iso_packet[kIsoPacket];
std::uint8_t configuration;
std::uint8_t alternate;
std::int16_t ring[USB_SPEAKER_BUFFER];
std::uint32_t head, tail;

std::uint16_t le16(const std::uint8_t* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }

void playback(bool on) {
    usb_speaker.playing = on;
    LPC_TIM0->TCR = on ? 1U : 2U;  // run, or hold in reset
}

void send(const std::uint8_t* data, std::uint32_t length) {
    const std::uint16_t wanted = le16(&setup[6]);
    usbd.EndpointTransfer(0x80, const_cast<std::uint8_t*>(data), length < wanted ? length : wanted);
}

void status_stage() { usbd.EndpointTransfer(0x80, nullptr, 0); }

void stall() {
    usbd.EndpointStall(0x80, true);
    usbd.EndpointStall(0x00, true);
}

void streaming(bool on) {
    if (on == (usb_speaker.streaming != 0)) return;
    usb_speaker.streaming = on;
    if (on) {
        usbd.EndpointConfigure(kIsoEndpoint, ARM_USB_ENDPOINT_ISOCHRONOUS, kIsoPacket);
        usbd.EndpointTransfer(kIsoEndpoint, iso_packet, kIsoPacket);
    } else {
        usbd.EndpointTransferAbort(kIsoEndpoint);
        usbd.EndpointUnconfigure(kIsoEndpoint);
        playback(false);
        head = tail = 0;
        usb_speaker.buffer_level = 0;
    }
}

void handle_setup() {
    const std::uint8_t type = setup[0] & 0x60;
    const std::uint8_t request = setup[1];
    const std::uint16_t value = le16(&setup[2]);
    const std::uint16_t index = le16(&setup[4]);
    if (type != 0) return stall();  // class and vendor requests: volume comes from the board
    switch (request) {
    case 6:  // GET_DESCRIPTOR
        if ((value >> 8) == 1) return send(kDevice, sizeof kDevice);
        if ((value >> 8) == 2) return send(kConfiguration, sizeof kConfiguration);
        return stall();
    case 5:  // SET_ADDRESS
        usbd.DeviceSetAddress(static_cast<std::uint8_t>(value & 0x7F));
        return status_stage();
    case 9:  // SET_CONFIGURATION
        configuration = static_cast<std::uint8_t>(value);
        usb_speaker.configured = configuration == 1;
        if (configuration == 0) streaming(false);
        return status_stage();
    case 8:  // GET_CONFIGURATION
        ep0_data[0] = configuration;
        return send(ep0_data, 1);
    case 0:  // GET_STATUS
        ep0_data[0] = ep0_data[1] = 0;
        return send(ep0_data, 2);
    case 11:  // SET_INTERFACE
        if (index == kStreamingInterface) {
            alternate = static_cast<std::uint8_t>(value);
            streaming(alternate == 1);
        }
        return status_stage();
    case 10:  // GET_INTERFACE
        ep0_data[0] = index == kStreamingInterface ? alternate : 0;
        return send(ep0_data, 1);
    default: return stall();
    }
}

void received(std::uint32_t bytes) {
    if (bytes == 0) return;  // a frame without a packet from the host
    usb_speaker.packets++;
    for (std::uint32_t i = 0; i + 1 < bytes; i += 2) {
        const auto sample = static_cast<std::int16_t>(iso_packet[i] | (iso_packet[i + 1] << 8));
        if (usb_speaker.buffer_level == USB_SPEAKER_BUFFER) {
            usb_speaker.overruns++;
            continue;
        }
        ring[head] = sample;
        head = (head + 1) % USB_SPEAKER_BUFFER;
        usb_speaker.buffer_level++;
        usb_speaker.samples_received++;
    }
    // Start playing once half the buffer is filled, as a jitter margin.
    if (!usb_speaker.playing && usb_speaker.buffer_level >= USB_SPEAKER_BUFFER / 2) playback(true);
}

void device_event(uint32_t event) {
    if (event == ARM_USBD_EVENT_RESET || event == ARM_USBD_EVENT_VBUS_OFF) {
        configuration = alternate = 0;
        usb_speaker.configured = 0;
        usb_speaker.streaming = 0;
        playback(false);
        head = tail = 0;
        usb_speaker.buffer_level = 0;
    }
}

void endpoint_event(uint8_t ep, uint32_t event) {
    if (ep == 0x00 && event == ARM_USBD_EVENT_SETUP) {
        if (usbd.ReadSetupPacket(setup) == ARM_DRIVER_OK) handle_setup();
    } else if (ep == kIsoEndpoint && event == ARM_USBD_EVENT_OUT) {
        received(usbd.EndpointTransferGetResult(kIsoEndpoint));
        if (usb_speaker.streaming) usbd.EndpointTransfer(kIsoEndpoint, iso_packet, kIsoPacket);
    }
}

}  // namespace

extern "C" {

void usb_speaker_timer_irq(void) {
    LPC_TIM0->IR = 1U;
    if (usb_speaker.buffer_level == 0) {  // nothing to play: stop until the buffer refills
        usb_speaker.underruns++;
        playback(false);
        return;
    }
    const std::int16_t sample = ring[tail];
    tail = (tail + 1) % USB_SPEAKER_BUFFER;
    usb_speaker.buffer_level--;
    usb_speaker.samples_played++;
    // The example's scaling: offset binary, times volume / 256.
    std::uint32_t scaled = (0x8000U + static_cast<std::uint16_t>(sample)) & 0xFFFFU;
    scaled = (scaled * usb_speaker.volume) >> 8;
    DAC_SetValue(scaled);
}

void usb_speaker_set_volume(uint32_t volume) { usb_speaker.volume = volume > 256 ? 256 : volume; }

void usb_speaker_start(void) {
    DAC_Initialize();
    LPC_SC->PCONP |= 1UL << 1;                                   // TIMER0
    LPC_SC->PCLKSEL0 = (LPC_SC->PCLKSEL0 & ~(3UL << 2)) | (1UL << 2);  // PCLK = CCLK
    LPC_TIM0->MR0 = SystemCoreClock / USB_SPEAKER_RATE - 1U;
    LPC_TIM0->MCR = 3U;  // interrupt and reset on MR0
    NVIC_EnableIRQ(TIMER0_IRQn);
    playback(false);
    usbd.Initialize(device_event, endpoint_event);
    usbd.PowerControl(ARM_POWER_FULL);
    usbd.DeviceConnect();
    usb_speaker.connected = 1;
}

void usb_speaker_stop(void) {
    streaming(false);
    usbd.DeviceDisconnect();
    usbd.PowerControl(ARM_POWER_OFF);
    usbd.Uninitialize();
    NVIC_DisableIRQ(TIMER0_IRQn);
    DAC_Uninitialize();
    usb_speaker.connected = 0;
    usb_speaker.configured = 0;
}

void usb_speaker_reset(void) {
    std::memset(const_cast<usb_speaker_status_t*>(&usb_speaker), 0, sizeof usb_speaker);
    usb_speaker.volume = 128;
    head = tail = 0;
    configuration = alternate = 0;
}

}  // extern "C"
