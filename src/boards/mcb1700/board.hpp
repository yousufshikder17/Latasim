#pragma once
// Keil MCB1700 board model: the LPC1768 plus what is wired to its pins.
// LEDs (outputs), the 5-way joystick and the INT0 button (inputs).
// Pin map: docs/phase0/mcb1700-pin-map.md.
#include <array>
#include <cstdint>

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

// LED polarity, the one place it is decided. Active-high: both Keil's board
// driver and the legacy LED.c turn an LED on by driving its pin high. Not yet
// confirmed on real hardware; one firmware source assumes active-low
// (docs/phase0/findings.md, LED polarity).
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

// INT0 push button: P2.10, low while pressed (MCB1700 guide, "INT0" jumper).
inline constexpr PinRef kInt0Pin{2, 10};

class Board {
public:
    Board();  // every input starts released

    lpc17xx::Lpc1768& mcu() { return mcu_; }
    const lpc17xx::Lpc1768& mcu() const { return mcu_; }

    LedState led(unsigned index) const;

    // Physical inputs. They drive the pin's external level, which firmware sees
    // while the pin is an input; the output latch is never touched.
    void press(JoystickDirection direction);
    void release(JoystickDirection direction);
    bool is_pressed(JoystickDirection direction) const;
    void press_int0();
    void release_int0();
    bool int0_pressed() const { return int0_pressed_; }

private:
    void drive_active_low(PinRef pin, bool pressed);

    lpc17xx::Lpc1768 mcu_;
    std::array<bool, kJoystickDirectionCount> joystick_pressed_{};
    bool int0_pressed_ = false;
};

}  // namespace latasim::mcb1700
