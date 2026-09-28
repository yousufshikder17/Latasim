#pragma once
// Keil MCB1700 board model: the LPC1768 plus what is wired to its pins.
// LEDs (outputs), the 5-way joystick and the INT0 button (inputs).
// Pin map: docs/phase0/mcb1700-pin-map.md.
#include <array>
#include <cstdint>
#include <vector>

#include "boards/mcb1700/glcd.hpp"
#include "lpc17xx/lpc1768.hpp"

namespace latasim::mcb1700 {

struct PinRef {
    unsigned port;
    unsigned pin;
};

inline constexpr unsigned kLedCount = 8;

// LED0-LED7 in the numbering of Keil's board driver (LED_MCB1700.c), which the
// legacy LED.c driver uses too. The board's silkscreen names them by pin.
inline constexpr std::array<PinRef, kLedCount> kLedPins{{
    {1, 28}, {1, 29}, {1, 31}, {2, 2}, {2, 3}, {2, 4}, {2, 5}, {2, 6},
}};

// LED polarity, the one place it is decided. Active-high: the MCB1700 schematic
// (sheet USB_COM_LED, rev 1.2) buffers all eight pins through a non-inverting
// 74LVC244 into LED anodes whose cathodes are grounded, and Keil's board driver
// drives high for on (docs/phase2/open-questions.md, question 4). Assumes the LED
// jumper is fitted; without it the buffer is disabled and every LED is dark.
inline constexpr bool kLedActiveHigh = true;

enum class LedState {
    Off,
    On,
    Undriven,  // the pin is not an output, so the model doesn't guess
};

const char* to_string(LedState state);

// The 5-way joystick. Each position grounds its pin while held (active-low); the
// pin is pulled high when released. Center is the "select" push.
enum class JoystickDirection { Center, Up, Right, Down, Left };
inline constexpr unsigned kJoystickDirectionCount = 5;
inline constexpr std::array<PinRef, kJoystickDirectionCount> kJoystickPins{{
    {1, 20},  // Center
    {1, 23},  // Up
    {1, 24},  // Right
    {1, 25},  // Down
    {1, 26},  // Left
}};

const char* to_string(JoystickDirection direction);

// INT0 push button: P2.10, low while pressed. Through the INT0 jumper the pin has
// an external 22k pull-up (R27) and the button shorts it to ground (schematic;
// docs/phase2/open-questions.md, question 2).
inline constexpr PinRef kInt0Pin{2, 10};

// The speaker: the DAC output AOUT (P0.26, PINSEL1[21:20] = 10) through the board's
// amplifier (docs/phase5/usb-audio.md). What reaches it is recorded as DAC values
// with their virtual times; analog behaviour (filtering, amplifier gain, the
// speaker itself) is not modelled.
struct SpeakerSample {
    std::uint64_t cycles;
    std::uint16_t value;  // DAC VALUE, 0-1023 (midscale 512)
    bool operator==(const SpeakerSample&) const = default;
};

// Traces (into mcu().trace()): each input that actually changes, and each LED
// whose visible state changes after an MMIO store.
// The potentiometer: AD0.2 on P0.25, 0-3.3 V (docs/phase0/mcb1700-pin-map.md).
inline constexpr unsigned kPotentiometerChannel = 2;

class Board {
public:
    Board();  // every input starts released
    Board(const Board&) = delete;  // the MCU's store hook refers to this board
    Board& operator=(const Board&) = delete;

    lpc17xx::Lpc1768& mcu() { return mcu_; }
    const lpc17xx::Lpc1768& mcu() const { return mcu_; }

    LedState led(unsigned index) const;

    // Physical inputs. They drive the pin's external level, which firmware sees
    // while the pin is an input; the output latch is never touched.
    void press(JoystickDirection direction);
    void release(JoystickDirection direction);
    bool is_pressed(JoystickDirection direction) const;
    // The potentiometer's position as the 12-bit value the ADC converts it to
    // (0 = 0 V .. 0xFFF = 3.3 V). Starts at 0.
    void set_potentiometer(std::uint32_t raw) { mcu_.set_analog_input(kPotentiometerChannel, raw); }
    std::uint32_t potentiometer() const { return mcu_.adc().input(kPotentiometerChannel); }

    // The GLCD (glcd.hpp): chip select is P0.6, watched after every store; bytes
    // arrive from SSP1's DR (register-level firmware) or through glcd_transfer (on
    // the host, the CMSIS-Driver Driver_SPI1).
    const Glcd& glcd() const { return glcd_; }
    std::uint8_t glcd_transfer(std::uint8_t mosi);

    void press_int0();
    void release_int0();
    bool int0_pressed() const { return int0_pressed_; }

    // Speaker output since the board was made (or since clear_speaker()), and the
    // same as signed 16-bit PCM: (value - 512) * 64.
    const std::vector<SpeakerSample>& speaker() const { return speaker_; }
    std::vector<std::int16_t> speaker_pcm() const;
    void clear_speaker() { speaker_.clear(); }

    // The board's USB device connector (the LPC1768's USB port): plug a host in, or
    // nullptr to unplug.
    void connect_usb_host(usb::HostPort* host) { mcu_.attach_usb_host(host); }

private:
    void drive_active_low(PinRef pin, bool pressed);
    void set_input(PinRef pin, bool& pressed, bool press);
    void trace_led_changes();
    void after_store();
    void trace_glcd_writes();

    lpc17xx::Lpc1768 mcu_;
    std::array<LedState, kLedCount> leds_shown_{};
    Glcd glcd_;
    std::array<bool, kJoystickDirectionCount> joystick_pressed_{};
    bool int0_pressed_ = false;
    std::vector<SpeakerSample> speaker_;
};

}  // namespace latasim::mcb1700
