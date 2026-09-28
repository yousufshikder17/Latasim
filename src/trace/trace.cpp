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
    if (address == kPclksel0Address) return "PCLKSEL0";
    if (address - kAdcBase < 0x38 && address % 4 == 0) {
        const std::uint32_t off = address - kAdcBase;
        if (off >= 0x10 && off <= 0x2C) {
            std::snprintf(text, sizeof text, "ADDR%u", static_cast<unsigned>((off - 0x10) / 4));
            return text;
        }
        static const char* const names[] = {"ADCR", "ADGDR", "0x40034008", "ADINTEN"};
        if (off < 0x10) return names[off / 4];
        if (off == 0x30) return "ADSTAT";
    }
    if (address == kPclksel1Address) return "PCLKSEL1";
    for (unsigned n = 0; n < kTimerBase.size(); ++n) {
        const std::uint32_t off = address - kTimerBase[n];
        if (off <= 0x24 && off % 4 == 0) {
            static const char* const names[] = {"IR", "TCR", "TC", "PR", "PC", "MCR", "MR0", "MR1", "MR2", "MR3"};
            std::snprintf(text, sizeof text, "T%u%s", n, names[off / 4]);
            return text;
        }
    }
    // NVIC (ARM DUI 0552A table 4-2): ISERn, ICERn, ISPRn, ICPRn, IABRn; IPRn by word,
    // PRI_n (the priority field of IRQ n) by byte; SHPR3, PRI_15 (SysTick).
    static const struct {
        std::uint32_t base;
        const char* name;
    } bitmaps[] = {{0xE000E100, "ISER"}, {0xE000E180, "ICER"}, {0xE000E200, "ISPR"}, {0xE000E280, "ICPR"},
                   {0xE000E300, "IABR"}};
    for (const auto& r : bitmaps)
        if (address == r.base || address == r.base + 4) {
            std::snprintf(text, sizeof text, "%s%u", r.name, static_cast<unsigned>((address - r.base) / 4));
            return text;
        }
    if (address >= 0xE000E400 && address < 0xE000E424) {
        if (width == 4) std::snprintf(text, sizeof text, "IPR%u", static_cast<unsigned>((address - 0xE000E400) / 4));
        else std::snprintf(text, sizeof text, "PRI_%u", static_cast<unsigned>(address - 0xE000E400));
        return text;
    }
    if (address == 0xE000ED20 && width == 4) return "SHPR3";
    if (address == 0xE000ED23 && width == 1) return "PRI_15";
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
    case TraceKind::AdcConversion:
        std::snprintf(op, sizeof op, "adc");
        std::snprintf(what, sizeof what, "AD0.%u", e.pin);
        std::snprintf(value, sizeof value, "0x%03X", static_cast<unsigned>(e.value));
        break;
    case TraceKind::TimerMatch: {
        std::snprintf(op, sizeof op, "match");
        std::snprintf(what, sizeof what, "%s", lpc17xx::irq_name(e.irq).c_str());
        std::string channels;
        for (unsigned n = 0; n < 4; ++n)
            if (e.value & (1u << n)) channels += (channels.empty() ? "MR" : ",MR") + std::to_string(n);
        std::snprintf(value, sizeof value, "%s", channels.c_str());
        break;
    }
    case TraceKind::Interrupt: {
        static const char* const phases[] = {"pend", "enter", "exit"};
        std::snprintf(op, sizeof op, "irq");
        std::snprintf(what, sizeof what, "%s", lpc17xx::irq_name(e.irq).c_str());
        std::snprintf(value, sizeof value, "%s", e.value < 3 ? phases[e.value] : "?");
        break;
    }
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
