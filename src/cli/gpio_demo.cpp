#include "cli/gpio_demo.hpp"

#include <cstdint>
#include <cstdio>
#include <string>

#include "boards/mcb1700/board.hpp"
#include "boards/mcb1700/keil_board_led.hpp"

namespace latasim::cli {
namespace {

using lpc17xx::bit_band_alias;
using lpc17xx::gpio_register_address;
using lpc17xx::GpioReg;
using mcb1700::Board;

std::string hex(std::uint32_t value) {
    char text[16];
    std::snprintf(text, sizeof text, "%08X", static_cast<unsigned>(value));
    return text;
}

void step(std::ostream& out, const Board& board, const std::string& action, unsigned led) {
    char line[160];
    std::snprintf(line, sizeof line, "  %-44s LED%u %-9s FIO1PIN=%s FIO2PIN=%s\n", action.c_str(), led,
                  mcb1700::to_string(board.led(led)),
                  hex(board.mcu().read32(gpio_register_address(1, GpioReg::Pin))).c_str(),
                  hex(board.mcu().read32(gpio_register_address(2, GpioReg::Pin))).c_str());
    out << line;
}

void store(std::ostream& out, Board& board, const char* name, std::uint32_t address, std::uint32_t value,
           unsigned led, const char* note = "") {
    board.mcu().write32(address, value);
    step(out, board, std::string(name) + " " + hex(address) + " <- " + hex(value) + note, led);
}

}  // namespace

void run_gpio_demo(std::ostream& out) {
    out << "MCB1700 GPIO demo: LED0 = P1.28, LED3 = P2.2, "
        << (mcb1700::kLedActiveHigh ? "active-high" : "active-low") << "\n\n";

    out << "Direct register access (firmware-style stores)\n";
    Board regs;
    step(out, regs, "reset", 0);
    store(out, regs, "FIO1DIR", gpio_register_address(1, GpioReg::Dir), 1u << 28, 0);
    store(out, regs, "FIO1SET", gpio_register_address(1, GpioReg::Set), 1u << 28, 0);
    store(out, regs, "FIO1CLR", gpio_register_address(1, GpioReg::Clr), 1u << 28, 0);
    store(out, regs, "alias  ", bit_band_alias(gpio_register_address(1, GpioReg::Pin), 28), 1, 0, " P1.28");
    store(out, regs, "alias  ", 0x233806EC, 0, 0, " P1.27!");
    store(out, regs, "alias  ", bit_band_alias(gpio_register_address(1, GpioReg::Pin), 28), 0, 0, " P1.28");
    out << "  (P1.27! is the wrong-pin alias from Phase 0: LED0 is unaffected)\n\n";

    out << "Keil board API (LED_*), same register model\n";
    Board api;
    mcb1700::KeilBoardLed leds(api.mcu());
    leds.initialize();
    step(out, api, "LED_Initialize()", 0);
    leds.on(0);
    step(out, api, "LED_On(0)", 0);
    leds.on(3);
    step(out, api, "LED_On(3)", 3);
    leds.off(0);
    step(out, api, "LED_Off(0)", 0);
    api.mcu().write32(gpio_register_address(2, GpioReg::Clr), 1u << 2);
    step(out, api, "FIO2CLR <- 00000004 (register store)", 3);

    out << "\nLEDs:";
    for (unsigned i = 0; i < mcb1700::kLedCount; ++i) out << " " << i << "=" << mcb1700::to_string(api.led(i));
    out << "\n";
}

}  // namespace latasim::cli
