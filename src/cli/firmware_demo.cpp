#include "cli/firmware_demo.hpp"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

extern "C" {  // Keil's headers have no C++ guards; the drivers are compiled as C
#include "Board_Buttons.h"
#include "Board_Joystick.h"
#include "Board_LED.h"
}

#include "boards/mcb1700/board.hpp"
#include "firmware/blinky_ulp.h"
#include "host/binding.hpp"
#include "lpc17xx/lpc1768.hpp"
#include "trace/trace.hpp"

namespace latasim::cli {
namespace {

using mcb1700::Board;

std::string leds(const Board& board) {
    std::string line = "LEDs:";
    for (unsigned i = 0; i < mcb1700::kLedCount; ++i)
        line += " " + std::to_string(i) + "=" + mcb1700::to_string(board.led(i));
    return line;
}

std::string hex2(std::uint32_t value) {
    char text[8];
    std::snprintf(text, sizeof text, "0x%02X", static_cast<unsigned>(value));
    return text;
}

// Prints the trace events recorded since the last call.
class TraceTail {
public:
    TraceTail(std::ostream& out, const Board& board) : out_(out), events_(board.mcu().trace().events()) {}

    void print() {
        for (; shown_ < events_.size(); ++shown_) out_ << "    " << to_string(events_[shown_]) << "\n";
    }
    void summarise(const char* what) {
        out_ << "    #" << shown_ + 1 << "-#" << events_.size() << ": " << events_.size() - shown_ << " events, "
             << what << "\n";
        shown_ = events_.size();
    }

private:
    std::ostream& out_;
    const std::vector<TraceEvent>& events_;
    std::size_t shown_ = 0;
};

void run_board_drivers(std::ostream& out) {
    out << "Part 1: board drivers\n"
           "Keil's LED_MCB1700.c, Joystick_MCB1700.c and Buttons_MCB1700.c (unmodified C).\n\n";

    Board board;
    host::FirmwareBinding bind(board);
    TraceTail trace(out, board);
    out << leds(board) << "\n\n";

    out << "firmware: LED_Initialize(); Joystick_Initialize(); Buttons_Initialize();\n";
    LED_Initialize();
    Joystick_Initialize();
    Buttons_Initialize();
    trace.summarise("PCONP, pin directions, LED pins driven low");
    out << leds(board) << "\n\n";

    out << "firmware: LED_On(0)\n";
    LED_On(0);
    trace.print();

    out << "\nboard:    joystick UP pressed\n";
    board.press(mcb1700::JoystickDirection::Up);
    trace.print();
    const uint32_t joystick = Joystick_GetState();
    out << "firmware: Joystick_GetState() = " << hex2(joystick)
        << (joystick == JOYSTICK_UP ? " (JOYSTICK_UP)" : "") << "\n";
    trace.print();

    out << "\nboard:    INT0 pressed\n";
    board.press_int0();
    trace.print();
    const uint32_t buttons = Buttons_GetState();
    out << "firmware: Buttons_GetState() = " << hex2(buttons) << (buttons == 1u ? " (INT0)" : "") << "\n";
    trace.print();
    out << "firmware: LED_SetOut(joystick | buttons)  (LED3: UP, LED0: INT0)\n";
    LED_SetOut(joystick | buttons);
    trace.print();

    out << "\nboard:    joystick and INT0 released\n";
    board.release(mcb1700::JoystickDirection::Up);
    board.release_int0();
    trace.print();

    out << "\n" << leds(board) << "\n" << board.mcu().trace().events().size() << " trace events\n";
}

void run_timed_firmware(std::ostream& out) {
    out << "Part 2: timed firmware, on a new board\n"
           "Keil's Blinky_ULp: SysTick_Handler in IRQ.c (unmodified C) steps an LED chase\n"
           "every 10 ms of virtual time.\n\n";

    Board board;
    host::FirmwareBinding bind(board);
    TraceTail trace(out, board);

    out << "firmware: LED_Initialize(); ...; SysTick_Config(SystemCoreClock / 100);\n";
    blinky_ulp_start();
    trace.summarise("LED pins driven low, SysTick every 1,000,000 cycles");
    board.mcu().bind_handler(lpc17xx::kSysTickIrq, SysTick_Handler);

    out << "\nrun for 25 ms (2,500,000 cycles)\n";
    board.mcu().advance_cycles(25 * lpc17xx::kCyclesPerMillisecond);
    trace.print();

    out << "\n" << leds(board) << "\n"
        << board.mcu().trace().events().size() << " trace events, t=" << board.mcu().cycles() << "\n";
}

}  // namespace

void run_firmware_demo(std::ostream& out) {
    out << "Latasim MCB1700 firmware demo\n"
           "Keil's MCB1700 sources on modelled boards. Indented lines are the hardware trace;\n"
           "t is virtual time in core clock cycles (100 MHz). Every run is identical.\n\n";
    run_board_drivers(out);
    out << "\n";
    run_timed_firmware(out);
}

}  // namespace latasim::cli
