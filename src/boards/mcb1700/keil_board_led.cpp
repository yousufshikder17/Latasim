#include "boards/mcb1700/keil_board_led.hpp"

#include "boards/mcb1700/board.hpp"

namespace latasim::mcb1700 {

std::int32_t KeilBoardLed::initialize() {
    for (const PinRef& p : kLedPins) {
        gpio_.set_dir(p.port, p.pin, true);
        gpio_.pin_write(p.port, p.pin, 0);
    }
    return 0;
}

std::int32_t KeilBoardLed::on(std::uint32_t num) {
    if (num >= kLedCount) return -1;
    gpio_.pin_write(kLedPins[num].port, kLedPins[num].pin, 1);
    return 0;
}

std::int32_t KeilBoardLed::off(std::uint32_t num) {
    if (num >= kLedCount) return -1;
    gpio_.pin_write(kLedPins[num].port, kLedPins[num].pin, 0);
    return 0;
}

std::int32_t KeilBoardLed::set_out(std::uint32_t val) {
    for (std::uint32_t n = 0; n < kLedCount; ++n) {
        if (val & (1u << n)) on(n);
        else off(n);
    }
    return 0;
}

std::uint32_t KeilBoardLed::count() const { return kLedCount; }

}  // namespace latasim::mcb1700
