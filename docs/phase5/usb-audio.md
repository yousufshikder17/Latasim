# USB device and audio

Phase 5 adds what media firmware needs to stream audio from a PC to the MCB1700's speaker:
- the LPC1768's USB device controller;
- its DAC;
- a virtual PC on the USB port;
- the board's speaker;
- firmware that ties them together.

## The reference: Keil's USB Audio example

`Boards/Keil/MCB1700/Middleware/USB/Device/Audio` in LPC1700_DFP 2.6.0 is a USB speaker:

| Aspect | Keil's example |
|---|---|
| **USB class** | USB Audio Class 1.0 speaker, isochronous OUT endpoint 3, 64-byte packets |
| **Format** | 32 kHz, mono, 16-bit PCM; a 2048-byte buffer (`USBD_Config_ADC_0.h`) |
| **Playback** | TIMER0 interrupts at the sample rate and write each sample to the DAC through `DAC_MCB1700.c`: `(0x8000 + sample) * volume >> 8` |
| **Volume** | Set by the host's audio-class requests |
| **RTOS** | CMSIS-RTOS2 (RTX5) |
| **USB stack** | MDK's USB device middleware, which ships only as ARM object code (`USB_CM3_L.lib`) |

**Why it can't run on the host:** the middleware is ARM object code, and the example uses RTOS2. What can run is Keil's register-level USB device driver underneath it, `RTE_Driver/USBD_LPC17xx.c` (Apache-2.0), with its companion `OTG_LPC17xx.c`.

## What runs

**Keil's code, unchanged from the pack**
- **USB device driver:** `USBD_LPC17xx.c`, compiled as C++ through `src/firmware/usb/usbd_lpc17xx.cpp`.
  - It is compiled without `LPC175x_6x`, so its header declares only register constants, and `LPC_USB` points at host register proxies.
  - The Cortex-M intrinsics it uses (LDREX/STREX for its busy flag, unaligned word access) get single-threaded host versions.
- **OTG/pin companion:** `OTG_LPC17xx.c` (USB pins, `USB_IRQHandler`).
- **DAC driver:** `DAC_MCB1700.c`.

The pin configuration is the USB Audio example's `RTE_Device.h`, from the pack. `RTE_Components.h` is Latasim's two-line equivalent of the file µVision generates.

**Latasim's representative firmware: `src/firmware/usb/usb_speaker.cpp`**
- **Format and playback:** the same format and scaling as Keil's example, written directly against the CMSIS USB device driver API.
- **Descriptors:** device and configuration descriptors for a UAC 1.0 speaker.
- **Standard requests:** GET_DESCRIPTOR, SET_ADDRESS, SET/GET_CONFIGURATION, SET/GET_INTERFACE, GET_STATUS.
- **Streaming:** on SET_INTERFACE alternate 1, endpoint 3 is configured and packets are received each frame into a 1024-sample ring buffer.
- **Playback:** TIMER0 plays at 32 kHz once the buffer is half full, and stops on an empty tick (an underrun) until it is half full again.
- **Volume:** the application sets it (`usb_speaker_set_volume`). Audio-class requests are stalled, since volume comes from the board's potentiometer.

The media center reads the potentiometer through the real ADC path: Keil's `ADC_MCB1700.c` and the modelled ADC. Its value sets that volume.

## The USB device controller model

`src/lpc17xx/usb_device.hpp` models UM10360 chapter 11 at its register interface, in slave mode.

**Registers**
- **Device interrupts:** USBDevIntSt/En/Clr/Set, driving IRQ 24.
- **SIE commands:** USBCmdCode/USBCmdData:
  - Set Address, Configure Device, Set Mode;
  - Read Frame Number, Read Test Register;
  - Set/Get Device Status, Get Error Code, Read Error Status;
  - Select Endpoint, Select Endpoint/Clear Interrupt, Set Endpoint Status;
  - Clear Buffer, Validate Buffer.
  Commands complete at once, and CCEMPTY/CDFULL are set by the store that issues them.
- **Packet FIFOs:** USBRxData/USBRxPLen/USBTxData/USBTxPLen/USBCtrl.
- **Endpoint interrupts:** USBEpIntSt/En/Clr/Set. A write to USBEpIntClr runs Select Endpoint/Clear Interrupt (11.10.3.3).
- **Realization:** USBReEp/USBEpInd/USBMaxPSize, which set EP_RLZED.
- **Clocks:** USBClkCtrl/St.

**Behaviour**
- **Isochronous endpoints:** data is exchanged at the frame interrupt. Setting RD_EN always gives PKT_RDY: this frame's packet, or an empty one with DV clear (11.14.2). Unread data is gone at the next frame.
- **Control endpoints:** a SETUP unstalls both control endpoints and invalidates a validated IN buffer.
- **Bus side** (`src/devices/usb.hpp`, generic): the host sends SETUP and OUT packets, takes IN packets (or sees NAK/STALL), delivers isochronous packets and marks frames.
- **Frames:** a host plugged into the port gets a frame every 1 ms of virtual time.

## The virtual PC

`src/devices/usb_audio_host.hpp` is generic: it works with any `usb::Bus`.

**Enumeration**
- **Pacing:** it runs one transaction per 1 ms frame.
- **Connect:** when the device connects itself, it resets the bus.
- **Standard requests:** device descriptor, SET_ADDRESS 1, configuration descriptor (header, then whole), SET_CONFIGURATION.
- **Stream selection:** it finds the first audio-streaming alternate setting with an isochronous OUT endpoint and a Type I format, and selects it.

**Streaming**
- **Packets:** it sends one packet per frame of `sample_rate / 1000` samples from a source:
  - `TonePcm`, an integer triangle wave;
  - `BufferPcm`, given samples, for example a WAV file loaded in the desktop.
- **Pause and disconnect:** pausing stops the packets. A device that disconnects itself is seen on the next frame, and enumerated again when it reconnects.

## Audio output

- **Output path:** the DAC's DACR (VALUE bits 15:6) drives AOUT. When PINSEL1 selects AOUT on P0.26, the board's speaker records each value with its virtual time (`Board::speaker()`).
- **PCM:** `speaker_pcm()` gives the same samples as signed 16-bit PCM.
- **Authoritative output:** the recorded samples. Playing them on the PC (desktop, optional) only reads them.

## Inspection

| Where | What |
|---|---|
| **Model** | Device connected/configured, address and frame number; the host's state, stream parameters and packet/sample counts; speaker samples and DAC value |
| **Firmware** | Streaming/playing, buffer level, volume, underruns, overruns, samples received/played (`usb_speaker` status) |

## Tests

| Suite | What it covers |
|---|---|
| `UsbDevice.*` (no packs) | SIE commands, realization, device status on cable and reset, SETUP and IN transfers, stalls, isochronous frames, IRQ 24, DAC |
| `UsbAudio.*` | Enumeration and stream parameters; playback at half buffer with no underruns and samples 3125 cycles apart; the DAC playing the host's samples in order at the example's scaling; volume scaling; pause and underrun; disconnect and re-enumeration; identical repeated runs |
| `Extensibility.UsbAudioHostDrivesAnotherDeviceController` | The host against a scripted device with no LPC1768 in it |

## Not modelled

- **Electrical and bus timing:** USB electrical behaviour, the PHY, bit timing, NRZI/CRC/toggles and handshake timing. Transfers are whole packets at frame granularity.
- **Controller features:** DMA mode, the host and OTG controllers, double buffering, suspend/resume timing, error conditions.
- **Clock drift:** host and device clocks agree exactly, so the feedback that Keil's example uses to adjust TIMER0 to the host's rate is unnecessary and omitted.
- **Audio:** the analog path after the DAC (filter, amplifier, speaker) and audio-class control requests.
