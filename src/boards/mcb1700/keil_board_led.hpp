#pragma once
// Host implementation of Keil's MCB1700 LED board API (Board_LED.h, implemented
// by Boards/Keil/MCB1700/Common/LED_MCB1700.c in LPC1700_DFP 2.6.0), built on the
// Keil GPIO driver. Return codes match: 0 = success, -1 = failure.
//
// Not modeled: PIN_Configure (PINCON pull-down setup in LED_Initialize) and
// LED_Uninitialize. Like the real driver, LED_On writes 1: the driver's own
// active-high assumption, independent of the board model's kLedActiveHigh.
#include <cstdint>

#include "lpc17xx/keil_gpio_driver.hpp"
#include "lpc17xx/lpc1768.hpp"

namespace vwb::mcb1700 {

class KeilBoardLed {
public:
    explicit KeilBoardLed(lpc17xx::Lpc1768& mcu) : gpio_(mcu) {}

    std::int32_t initialize();                 // LED_Initialize
    std::int32_t on(std::uint32_t num);        // LED_On
    std::int32_t off(std::uint32_t num);       // LED_Off
    std::int32_t set_out(std::uint32_t val);   // LED_SetOut
    std::uint32_t count() const;               // LED_GetCount

private:
    lpc17xx::KeilGpioDriver gpio_;
};

}  // namespace vwb::mcb1700
