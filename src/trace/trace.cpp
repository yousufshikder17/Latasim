#include "trace/trace.hpp"

#include <cstdio>

#include "boards/mcb1700/board.hpp"
#include "lpc17xx/lpc1768.hpp"

namespace latasim {
namespace {

// "FIO1SET", "FIO1PIN3" (byte 3), "FIO2DIRH" (upper halfword), "PCONP", or the
// address in hex for anything else (bit-band aliases).
std::string register_name(std::uint32_t address, unsigned width) {
    using namespace lpc17xx;
    char text[32];
    if (address == kPconpAddress) return "PCONP";
    if (address - kSysTickBase < 0x10 && address % 4 == 0) {
        static const char* const systick[] = {"STCTRL", "STRELOAD", "STCURR", "STCALIB"};
        return systick[(address - kSysTickBase) / 4];
    }
    const std::uint32_t offset = address - kGpioBase;
    if (address >= kGpioBase && offset < Gpio::kPortCount * kGpioPortStride) {
        static const char* const names[] = {"DIR", nullptr, nullptr, nullptr, "MASK", "PIN", "SET", "CLR"};
        const std::uint32_t in_port = offset % kGpioPortStride;
        const char* reg = names[in_port / 4];
        if (reg) {
            const unsigned lane = in_port % 4;
            char suffix[2] = {};
            if (width == 1) suffix[0] = static_cast<char>('0' + lane);
            if (width == 2) suffix[0] = lane == 0 ? 'L' : 'H';
            std::snprintf(text, sizeof text, "FIO%u%s%s", static_cast<unsigned>(offset / kGpioPortStride), reg,
                          suffix);
            return text;
        }
    }
    std::snprintf(text, sizeof text, "0x%08X", static_cast<unsigned>(address));
    return text;
}

}  // namespace

std::string to_string(const TraceEvent& e) {
    char op[16] = {}, what[32] = {}, value[16] = {};
    switch (e.kind) {
    case TraceKind::Read:
    case TraceKind::Write:
        std::snprintf(op, sizeof op, "%s%u", e.kind == TraceKind::Read ? "read" : "write", e.width * 8);
        std::snprintf(what, sizeof what, "%s", register_name(e.address, e.width).c_str());
        std::snprintf(value, sizeof value, "0x%0*X", static_cast<int>(e.width * 2), static_cast<unsigned>(e.value));
        break;
    case TraceKind::Input:
        std::snprintf(op, sizeof op, "input");
        std::snprintf(what, sizeof what, "P%u.%u", e.port, e.pin);
        std::snprintf(value, sizeof value, "%s", e.value ? "high" : "low");
        break;
    case TraceKind::Led:
        std::snprintf(op, sizeof op, "led");
        std::snprintf(what, sizeof what, "LED%u", e.led);
        std::snprintf(value, sizeof value, "%s", mcb1700::to_string(static_cast<mcb1700::LedState>(e.value)));
        break;
    case TraceKind::SysTick:
        std::snprintf(op, sizeof op, "systick");
        std::snprintf(what, sizeof what, "handler");
        break;
    }
    char time[32];
    std::snprintf(time, sizeof time, "t=%llu", static_cast<unsigned long long>(e.cycles));
    char line[128];
    std::snprintf(line, sizeof line, "#%-4llu %-12s %-7s %-9s %s", static_cast<unsigned long long>(e.seq), time, op,
                  what, value);
    std::string text = line;
    while (!text.empty() && text.back() == ' ') text.pop_back();  // no value: no trailing blanks
    return text;
}

}  // namespace latasim
