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
    if (address == kExtintAddress) return "EXTINT";
    if (address == kExtmodeAddress) return "EXTMODE";
    if (address == kExtpolarAddress) return "EXTPOLAR";
    if (address - kPinconBase < kPinconWindow && address % 4 == 0) {
        const std::uint32_t off = address - kPinconBase;
        if (off <= 0x28) std::snprintf(text, sizeof text, "PINSEL%u", static_cast<unsigned>(off / 4));
        else if (off >= 0x40 && off <= 0x64) std::snprintf(text, sizeof text, "PINMODE%u", static_cast<unsigned>((off - 0x40) / 4));
        else if (off >= 0x68 && off <= 0x78) std::snprintf(text, sizeof text, "PINMODEOD%u", static_cast<unsigned>((off - 0x68) / 4));
        else if (off == 0x7C) return "I2CPADCFG";
        else std::snprintf(text, sizeof text, "0x%08X", static_cast<unsigned>(address));
        return text;
    }
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
    if (address == kDacBase) return "DACR";
    if (address - kUsbBase >= 0x200 && address - kUsbBase <= 0x24C && address % 4 == 0) {
        static const char* const usb[] = {"USBDevIntSt", "USBDevIntEn", "USBDevIntClr", "USBDevIntSet", "USBCmdCode",
                                          "USBCmdData",  "USBRxData",   "USBTxData",    "USBRxPLen",    "USBTxPLen",
                                          "USBCtrl",     "USBDevIntPri", "USBEpIntSt",  "USBEpIntEn",   "USBEpIntClr",
                                          "USBEpIntSet", "USBEpIntPri", "USBReEp",      "USBEpInd",     "USBMaxPSize"};
        return usb[(address - kUsbBase - 0x200) / 4];
    }
    if (address - kSsp1Base <= 0x10 && address % 4 == 0) {
        static const char* const ssp[] = {"SSP1CR0", "SSP1CR1", "SSP1DR", "SSP1SR", "SSP1CPSR"};
        return ssp[(address - kSsp1Base) / 4];
    }
    if (address == kUsbBase + 0xFF4) return "USBClkCtrl";
    if (address == kUsbBase + 0xFF8) return "USBClkSt";
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

// RTX levels as osPriority names; 0 is the idle demon, 255 the kernel-start boost.
const char* priority_name(int level) {
    static const char* const names[] = {"Demon", "Idle", "Low", "BelowNormal", "Normal", "AboveNormal", "High",
                                        "Realtime"};
    return level >= 0 && level < 8 ? names[level] : "Max";
}

void rtos_text(const TraceEvent& e, char* value, std::size_t size) {
    const char* p = priority_name(e.priority);
    const unsigned v = e.value, o = e.object;
    switch (e.op) {
    case RtosOp::KernelStart: std::snprintf(value, size, "kernel-start %s", p); break;
    case RtosOp::Create: std::snprintf(value, size, "create %s", p); break;
    case RtosOp::Run: std::snprintf(value, size, "run %s", p); break;
    case RtosOp::Preempt: std::snprintf(value, size, "preempt %s", p); break;
    case RtosOp::Yield: std::snprintf(value, size, "yield"); break;
    case RtosOp::Delay: std::snprintf(value, size, "delay %u", v); break;
    case RtosOp::Wake: std::snprintf(value, size, "wake"); break;
    case RtosOp::Terminate: std::snprintf(value, size, "terminate"); break;
    case RtosOp::SignalSet: std::snprintf(value, size, "signal-set T%u 0x%04X", o, v); break;
    case RtosOp::SignalClear: std::snprintf(value, size, "signal-clear T%u 0x%04X", o, v); break;
    case RtosOp::SignalWait: std::snprintf(value, size, "signal-wait 0x%04X", v); break;
    case RtosOp::SignalWake: std::snprintf(value, size, "signal-wake 0x%04X", v); break;
    case RtosOp::MutexAcquire: std::snprintf(value, size, "mutex-acquire M%u", o); break;
    case RtosOp::MutexBlock: std::snprintf(value, size, "mutex-block M%u", o); break;
    case RtosOp::MutexRelease: std::snprintf(value, size, "mutex-release M%u", o); break;
    case RtosOp::PriorityChange:
        std::snprintf(value, size, "priority %s->%s", priority_name(static_cast<int>(v)), p);
        break;
    case RtosOp::PriorityInherit: std::snprintf(value, size, "inherit %s M%u", p, o); break;
    case RtosOp::PriorityRestore: std::snprintf(value, size, "restore %s M%u", p, o); break;
    case RtosOp::TimerStart: std::snprintf(value, size, "timer-start Tm%u %u", o, v); break;
    case RtosOp::TimerStop: std::snprintf(value, size, "timer-stop Tm%u", o); break;
    case RtosOp::TimerFire: std::snprintf(value, size, "timer-fire Tm%u", o); break;
    case RtosOp::TimerCallback: std::snprintf(value, size, "timer-callback Tm%u", o); break;
    }
}

}  // namespace

void Trace::set_mmio_capacity(std::size_t capacity) { mmio_capacity_ = capacity < 2 ? 2 : capacity; }

// Keeps the newest half of the capacity's MMIO accesses and every other event, in
// order. Amortised over the accesses that filled the other half.
void Trace::drop_oldest_mmio() {
    std::size_t excess = mmio_ - mmio_capacity_ / 2;
    const auto is_mmio = [](const TraceEvent& e) { return e.kind == TraceKind::Read || e.kind == TraceKind::Write; };
    auto out = events_.begin();
    for (auto in = events_.begin(); in != events_.end(); ++in) {
        if (excess > 0 && is_mmio(*in)) {
            --excess;
            ++dropped_;
            --mmio_;
            continue;
        }
        *out++ = *in;
    }
    events_.erase(out, events_.end());
}

TraceParts describe(const TraceEvent& e) {
    char op[16] = {}, what[32] = {}, value[48] = {};
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
    case TraceKind::Display:
        std::snprintf(op, sizeof op, "glcd");
        if (e.address == 0x22) {
            std::snprintf(what, sizeof what, "GRAM");
            std::snprintf(value, sizeof value, "%u px", static_cast<unsigned>(e.value));
        } else {
            std::snprintf(what, sizeof what, "R%02X", static_cast<unsigned>(e.address));
            std::snprintf(value, sizeof value, "0x%04X", static_cast<unsigned>(e.value));
        }
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
    case TraceKind::Rtos:
        std::snprintf(op, sizeof op, "rtos");
        if (e.thread == 0) std::snprintf(what, sizeof what, "idle");
        else if (e.thread == kIsrThread) std::snprintf(what, sizeof what, "isr");
        else std::snprintf(what, sizeof what, "T%u", static_cast<unsigned>(e.thread));
        rtos_text(e, value, sizeof value);
        break;
    case TraceKind::Interrupt: {
        static const char* const phases[] = {"pend", "enter", "exit"};
        std::snprintf(op, sizeof op, "irq");
        std::snprintf(what, sizeof what, "%s", lpc17xx::irq_name(e.irq).c_str());
        std::snprintf(value, sizeof value, "%s", e.value < 3 ? phases[e.value] : "?");
        break;
    }
    }
    const char* category = "mmio";
    switch (e.kind) {
    case TraceKind::Read:
    case TraceKind::Write:
        if (e.address - lpc17xx::kUsbBase < 0x1000 || e.address - lpc17xx::kDacBase < 0x10) category = "usb/audio";
        break;
    case TraceKind::Input: category = "input"; break;
    case TraceKind::Led: category = "led"; break;
    case TraceKind::Interrupt: category = "interrupt"; break;
    case TraceKind::TimerMatch: category = "timer"; break;
    case TraceKind::AdcConversion: category = "adc"; break;
    case TraceKind::Display: category = "glcd"; break;
    case TraceKind::Rtos: category = "rtos"; break;
    }
    return {category, op, what, value};
}

std::string to_string(const TraceEvent& e) {
    const TraceParts parts = describe(e);
    char time[32];
    std::snprintf(time, sizeof time, "t=%llu", static_cast<unsigned long long>(e.cycles));
    char line[160];
    std::snprintf(line, sizeof line, "#%-4llu %-12s %-7s %-9s %s", static_cast<unsigned long long>(e.seq), time,
                  parts.operation.c_str(), parts.subject.c_str(), parts.value.c_str());
    std::string text = line;
    while (!text.empty() && text.back() == ' ') text.pop_back();  // no value: no trailing blanks
    return text;
}

}  // namespace latasim
