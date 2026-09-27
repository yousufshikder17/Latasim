#include "boards/mcb1700/board.hpp"

#include <stdexcept>
#include <string>

namespace latasim::mcb1700 {

const char* to_string(LedState state) {
    switch (state) {
    case LedState::Off: return "OFF";
    case LedState::On: return "ON";
    case LedState::Undriven: return "UNDRIVEN";
    }
    return "?";
}

const char* to_string(JoystickDirection direction) {
    switch (direction) {
    case JoystickDirection::Center: return "center";
    case JoystickDirection::Up: return "up";
    case JoystickDirection::Right: return "right";
    case JoystickDirection::Down: return "down";
    case JoystickDirection::Left: return "left";
    }
    return "?";
}

Board::Board() {
    for (const PinRef& p : kJoystickPins) drive_active_low(p, false);
    drive_active_low(kInt0Pin, false);
}

void Board::drive_active_low(PinRef pin, bool pressed) {
    mcu_.gpio().set_external_level(pin.port, pin.pin, !pressed);
}

void Board::press(JoystickDirection direction) {
    const auto i = static_cast<unsigned>(direction);
    joystick_pressed_[i] = true;
    drive_active_low(kJoystickPins[i], true);
}

void Board::release(JoystickDirection direction) {
    const auto i = static_cast<unsigned>(direction);
    joystick_pressed_[i] = false;
    drive_active_low(kJoystickPins[i], false);
}

bool Board::is_pressed(JoystickDirection direction) const {
    return joystick_pressed_[static_cast<unsigned>(direction)];
}

void Board::press_int0() {
    int0_pressed_ = true;
    drive_active_low(kInt0Pin, true);
}

void Board::release_int0() {
    int0_pressed_ = false;
    drive_active_low(kInt0Pin, false);
}

LedState Board::led(unsigned index) const {
    if (index >= kLedCount) throw std::out_of_range("LED " + std::to_string(index) + " out of range");
    const PinRef p = kLedPins[index];
    const lpc17xx::Gpio& gpio = mcu_.gpio();
    if (!gpio.is_output(p.port, p.pin)) return LedState::Undriven;
    return gpio.pin_level(p.port, p.pin) == kLedActiveHigh ? LedState::On : LedState::Off;
}

}  // namespace latasim::mcb1700
