#pragma once
// The Cortex-M3 system timer, SysTick: the subset firmware uses to measure time.
// Sources: ARM DUI 0552A (Cortex-M3 Devices Generic User Guide) section 4.4, and
// UM10360 chapter 23 for the LPC17xx reset and calibration values.
//
// A 24-bit down counter clocked by the core clock (CLKSOURCE = 1). Setting ENABLE
// loads RELOAD into the counter at once, whatever it held (ARM 4.4.1; the LPC1768
// simulator does the same, E11 and E12). While enabled, each clock either wraps a zero counter to RELOAD or
// decrements it; reaching 0 from 1 sets COUNTFLAG. So after SysTick_Config(N) the
// counter first reaches 0 after N-1 clocks and then every N clocks, and RELOAD = 0
// never counts to 0 (ARM 4.4.2).
//
// Not modeled here: the SysTick exception. Lpc1768::advance_cycles calls an attached
// handler instead (see on_systick). Also not modeled: the external STCLK clock (with
// CLKSOURCE = 0 the counter does not advance) and halting in debug.
#include <cstdint>

namespace latasim::lpc17xx {

inline constexpr std::uint32_t kSysTickBase = 0xE000E010;  // SysTick_BASE, core_cm3.h

enum class SysTickReg : std::uint32_t {
    Ctrl = 0x0,   // STCTRL / SYST_CSR
    Load = 0x4,   // STRELOAD / SYST_RVR
    Val = 0x8,    // STCURR / SYST_CVR
    Calib = 0xC,  // STCALIB / SYST_CALIB (read-only)
};

inline constexpr std::uint32_t kSysTickEnable = 1u << 0;
inline constexpr std::uint32_t kSysTickTickint = 1u << 1;
inline constexpr std::uint32_t kSysTickClksource = 1u << 2;
inline constexpr std::uint32_t kSysTickCountflag = 1u << 16;
inline constexpr std::uint32_t kSysTickCounterMask = 0x00FFFFFF;  // RELOAD, CURRENT: 24 bits
// STCALIB reset value (UM10360 Table 442): TENMS = 999999, a 10 ms period at
// 100 MHz; SKEW = 0 (exact); NOREF = 0 (STCLK exists).
inline constexpr std::uint32_t kSysTickCalib = 0x000F423F;

class SysTick {
public:
    // Register reads. Reading CTRL returns COUNTFLAG and clears it (ARM 4.4.1).
    // The reset values are UM10360's; the simulator reads STCTRL and STCALIB as 0
    // at reset (E11, docs/phase3/open-questions.md).
    // Reserved bits read as 0.
    std::uint32_t read(SysTickReg reg);
    // What read(reg) returns, without clearing COUNTFLAG.
    std::uint32_t peek(SysTickReg reg) const;
    // CTRL keeps ENABLE/TICKINT/CLKSOURCE, LOAD keeps 24 bits, and any write to VAL
    // clears the counter and COUNTFLAG. CALIB is read-only: the caller rejects it.
    void write(SysTickReg reg, std::uint32_t value);

    // Advances the counter by `cycles` core clocks. Returns how many times it
    // counted from 1 to 0 (each of which sets COUNTFLAG). Does nothing unless
    // enabled with CLKSOURCE = core clock.
    std::uint64_t advance(std::uint64_t cycles);

    // Clocks until the counter next reaches 0 from 1, or 0 if it never will
    // (disabled, clocked by STCLK, or stuck at 0 with RELOAD 0).
    std::uint64_t cycles_to_zero() const;
    // The SysTick exception would be requested on each count to 0.
    bool interrupt_enabled() const { return (ctrl_ & kSysTickTickint) != 0; }

    // Observation without side effects.
    std::uint32_t ctrl() const { return ctrl_; }
    std::uint32_t reload() const { return reload_; }
    std::uint32_t current() const { return current_; }
    bool countflag() const { return countflag_; }

private:
    std::uint32_t ctrl_ = kSysTickClksource;  // UM10360 Table 438: STCTRL resets to 0x4
    std::uint32_t reload_ = 0;                // Table 438: 0
    std::uint32_t current_ = 0;               // Table 438: 0
    bool countflag_ = false;
};

}  // namespace latasim::lpc17xx
