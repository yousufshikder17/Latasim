# Phase 3: timing questions reviewed

What the Phase 3 timing work leaves uncertain, and where each question stands. Evidence:
- **ARM DUI 0552A** §4.4 and **UM10360** chapter 23, both in the device pack;
- simulator experiments **E11** (Keil's stock Blinky_ULp) and **E12** (SysTick register semantics from firmware), both under `spikes/uvsim-script/`.

| # | Question | Outcome |
|---|---|---|
| 1 | When does SysTick first count to 0 after being enabled? | **RESOLVED** |
| 2 | Does ENABLE load RELOAD when STCURR isn't 0? | **RESOLVED** |
| 3 | COUNTFLAG and STCURR write semantics | **RESOLVED** |
| 4 | SysTick reset values | **BOUNDED** |
| 5 | Is STCALIB writable? | **BOUNDED** |
| 6 | SysTick exception timing and priorities | **DEFERRED** (NVIC) |
| 7 | CPU-cycle fidelity of host-executed firmware | **DEFERRED** (B4) |
| 8 | Fixed 100 MHz core clock | **BOUNDED** |
| 9 | STCLK external clock source | **DEFERRED** |
| 10 | Firmware static state between runs in one process | **BOUNDED** |
| 11 | Real-hardware confirmation | **DEFERRED** |

## 1. First count to 0: RESOLVED
- **Question.** The model first loaded RELOAD on the clock after ENABLE, so the first count to 0 came RELOAD+1 cycles after `SysTick_Config`.
- **Evidence.** In E11, the stock Blinky_ULp's counter holds RELOAD at the instant `SysTick_Config` sets ENABLE (state 11379). It reaches 0 999,999 cycles later, then every 1,000,000 cycles. ARM 4.4.1 says the same: "When ENABLE is set to 1, the counter loads the RELOAD value".
- **Action.** Fixed in `ced01fd`. With the fix, Latasim's first tick and period match E11.

## 2. ENABLE with STCURR ≠ 0: RESOLVED
- **Evidence.** In E12, re-enabling with STCURR = 989 and RELOAD = 500 gives 500 (read 3 clocks later as 497).
- **Result.** Every ENABLE rising edge loads RELOAD, which is what the model does. It's pinned in `SysTick.EnablingLoadsReloadEvenMidCount`.

## 3. COUNTFLAG and STCURR writes: RESOLVED
In E12, the simulator matches the model and ARM 4.4 on each point:
- **COUNTFLAG** is set on counting from 1 to 0, and cleared by the STCTRL read that returns it.
- **A write to STCURR** clears it, and the next clock wraps to RELOAD.
- **Disabling** holds the counter.

## 4. Reset values: BOUNDED
- **The manual.** UM10360 Table 438 gives STCTRL `0x4` (CLKSOURCE = CPU clock) and STCALIB `0x000F423F`. The model uses these.
- **The simulator.** It reads both as 0 (E11, and from firmware in E12).
  - For STCALIB, the manual says the value is "initialized by the Boot Code". The simulator does not run the boot ROM, which accounts for 0 there.
  - For STCTRL, ARM 4.4.1 gives `0x0` for a device with a reference clock, and the LPC17xx has one (STCLK). The two documents disagree.
- **Impact:** none on the firmware used here. CMSIS `SysTick_Config` writes all of STCTRL.

**To close:** read STCTRL and STCALIB on a real board straight after reset.

## 5. STCALIB writability: BOUNDED
- **The sources disagree.** ARM gives it as read-only; UM10360 lists it as R/W.
- **The model faults on a write** rather than guessing, and no firmware here writes it.

## 6. Exception timing and priorities: DEFERRED (to NVIC work)
**`SysTick_Handler` runs synchronously when the counter reaches 0 with TICKINT set.** Not modelled:
- the NVIC, exception priorities (`SysTick_Config`'s `NVIC_SetPriority` is omitted), pending state and preemption;
- exception entry and exit time.

**In the simulator, the handler's first LED store lands about 114 cycles after the count to 0.** Its last store lands about 491 cycles after (E11). Latasim runs the handler in zero virtual time, so observable times differ by that much.

**This is exact enough for tick-driven firmware.** Correct interrupt timing needs an interrupt model.

## 7. Cycle fidelity of host firmware: DEFERRED (to a CPU backend, B4)
- **Host-executed firmware takes no virtual time.** Only `advance_cycles` moves the clock.
- **Firmware that measures its own execution time can't run faithfully,** for example busy-wait delays or polling a counter against instruction timing.
- **Doing so needs instruction execution:** a CPU emulator, or B3.

## 8. Fixed 100 MHz core clock: BOUNDED
- **The model assumes `kCoreClockHz` = 100 MHz,** the frequency Keil's MCB1700 `SystemInit` configures (E2).
- **Firmware that sets another PLL configuration would run at the wrong virtual rate,** and nothing detects it: there is no clock-tree model.
- **Within the MCB1700 projects examined, the assumption holds.**

## 9. STCLK clock source: DEFERRED
- **With CLKSOURCE = 0, the modelled counter holds,** because no STCLK (P3.26) clock is modelled.
- **No examined firmware uses it.**

## 10. Firmware statics between runs: BOUNDED
- **Host-compiled firmware keeps its globals and function statics for the life of the process.** There is no C startup to re-initialise them. For example, `IRQ.c` keeps its tick counter and chase position.
- **Every scenario in one process therefore starts from the previous one's firmware state,** unless it is reset.
- **The tests reset Blinky_ULp's statics** by running a scratch board to the state that recurs every 200 ticks (`tests/blinky_ulp_fixture.hpp`). Other firmware needs its own reset or a fresh process.
- **A new `latasim` process always starts fresh.**

## 11. Real hardware: DEFERRED
- **Every timing check is against the simulator and the documents.**
- **The open Phase 2 hardware questions carry over:** narrow FIOPIN stores, and INT0's level with the pull-down.
- **So do questions 4 and 5 above.**
