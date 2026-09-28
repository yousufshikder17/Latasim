#include "workbench/views.hpp"

#include "lpc17xx/lpc1768.hpp"
#include "lpc17xx/nvic.hpp"
#include "workbench/session.hpp"

namespace latasim::workbench {
namespace {

std::vector<RegisterGroup> make_groups() {
    std::vector<RegisterGroup> g;
    RegisterGroup gpio{"GPIO", {}};
    static const char* const regs[] = {"DIR", "MASK", "PIN"};
    static const std::uint32_t offs[] = {0x00, 0x10, 0x14};
    for (unsigned port = 0; port < 5; ++port)
        for (unsigned r = 0; r < 3; ++r)
            gpio.registers.push_back({"FIO" + std::to_string(port) + regs[r], 0x2009C000 + port * 0x20 + offs[r]});
    g.push_back(gpio);
    g.push_back({"SysTick", {{"STCTRL", 0xE000E010}, {"STRELOAD", 0xE000E014}, {"STCURR", 0xE000E018}, {"STCALIB", 0xE000E01C}}});
    g.push_back({"NVIC",
                 {{"ISER0", 0xE000E100}, {"ISER1", 0xE000E104}, {"ISPR0", 0xE000E200}, {"ISPR1", 0xE000E204},
                  {"IABR0", 0xE000E300}, {"IABR1", 0xE000E304}, {"IPR0", 0xE000E400}, {"IPR1", 0xE000E404},
                  {"IPR4", 0xE000E410}, {"IPR5", 0xE000E414}, {"IPR6", 0xE000E418}, {"SHPR3", 0xE000ED20}}});
    RegisterGroup timers{"Timers", {}};
    static const char* const tregs[] = {"IR", "TCR", "TC", "PR", "PC", "MCR", "MR0", "MR1", "MR2", "MR3"};
    for (unsigned n = 0; n < 4; ++n)
        for (unsigned r = 0; r < 10; ++r)
            timers.registers.push_back({"T" + std::to_string(n) + tregs[r], lpc17xx::kTimerBase[n] + 4 * r});
    g.push_back(timers);
    RegisterGroup adc{"ADC", {{"ADCR", 0x40034000}, {"ADGDR", 0x40034004}, {"ADINTEN", 0x4003400C}}};
    for (unsigned n = 0; n < 8; ++n) adc.registers.push_back({"ADDR" + std::to_string(n), 0x40034010 + 4 * n});
    adc.registers.push_back({"ADSTAT", 0x40034030});
    g.push_back(adc);
    RegisterGroup pincon{"PINCON / EINT", {}};
    for (unsigned n : {0u, 1u, 2u, 3u, 4u, 7u, 8u, 9u, 10u})
        pincon.registers.push_back({"PINSEL" + std::to_string(n), 0x4002C000 + 4 * n});
    pincon.registers.push_back({"EXTINT", 0x400FC140});
    pincon.registers.push_back({"EXTMODE", 0x400FC148});
    pincon.registers.push_back({"EXTPOLAR", 0x400FC14C});
    pincon.registers.push_back({"PCONP", 0x400FC0C4});
    pincon.registers.push_back({"PCLKSEL0", 0x400FC1A8});
    pincon.registers.push_back({"PCLKSEL1", 0x400FC1AC});
    g.push_back(pincon);
    g.push_back({"USB",
                 {{"USBDevIntSt", 0x5000C200}, {"USBDevIntEn", 0x5000C204}, {"USBCmdData", 0x5000C214},
                  {"USBRxPLen", 0x5000C220}, {"USBCtrl", 0x5000C228}, {"USBEpIntSt", 0x5000C230},
                  {"USBEpIntEn", 0x5000C234}, {"USBReEp", 0x5000C244}, {"USBClkCtrl", 0x5000CFF4},
                  {"USBClkSt", 0x5000CFF8}}});
    g.push_back({"DAC", {{"DACR", 0x4008C000}, {"DACCTRL", 0x4008C004}, {"DACCNTVAL", 0x4008C008}}});
    return g;
}

}  // namespace

const std::vector<RegisterGroup>& register_groups() {
    static const std::vector<RegisterGroup> groups = make_groups();
    return groups;
}

std::uint32_t register_value(const Session& session, std::uint32_t address) {
    return session.board().mcu().peek32(address);
}

Timeline timeline(const Session& session) {
    Timeline out;
    out.end = session.now();
    const rtos::Kernel* kernel = session.kernel();
    if (kernel != nullptr) {
        for (const auto& t : kernel->threads()) out.threads.push_back({t.id, t.name, {}, {}});
        for (const auto& i : kernel->timeline())
            if (i.thread < out.threads.size()) out.threads[i.thread].intervals.push_back(i);
    }
    for (const auto& e : session.board().mcu().trace().events()) {
        if (e.kind == TraceKind::Interrupt && e.value == kInterruptEnter) {
            out.interrupts.push_back({e.cycles, lpc17xx::irq_name(e.irq)});
        } else if (e.kind == TraceKind::Rtos && e.thread < out.threads.size()) {
            switch (e.op) {
            case RtosOp::SignalSet:
            case RtosOp::SignalWake:
            case RtosOp::MutexAcquire:
            case RtosOp::MutexBlock:
            case RtosOp::MutexRelease:
            case RtosOp::PriorityChange:
            case RtosOp::PriorityInherit:
            case RtosOp::PriorityRestore:
            case RtosOp::TimerCallback:
            case RtosOp::Yield:
                out.threads[e.thread].markers.push_back({e.cycles, describe(e).value});
                break;
            default: break;
            }
        }
    }
    return out;
}

}  // namespace latasim::workbench
