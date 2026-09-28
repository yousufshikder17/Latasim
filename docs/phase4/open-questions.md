# Phase 4: interrupt and peripheral questions reviewed

What the Phase 4 interrupt controller, timers, ADC, EINT0 and GLCD models leave uncertain, and where each question stands. Evidence:
- **ARM DUI 0552A** (Cortex-M3 Devices Generic User Guide) §2.3 and §4.2, and **UM10360** chapters 3, 4, 8, 9, 21 and 29, all in the device pack;
- the MCB1700 schematic and Keil's MCB1700 board drivers (`GLCD_MCB1700.c`, `ADC_MCB1700.c`), in the pack;
- simulator experiment **E13** (Phase 4 peripherals from firmware), under `spikes/uvsim-script/` (`e13-phase4.ini`, `e13-phase4-firmware.c`, two identical runs). Also E6 (GLCD SPI bytes) and E11 (SysTick handler timing) from earlier phases.

E13 timestamps come from TIMER1 counting at CCLK, read by polling firmware, so each includes a polling latency of about 10 cycles.

| # | Question | Outcome |
|---|---|---|
| 1 | Arbitration between simultaneous exceptions | **RESOLVED** |
| 2 | Level lines after a software clear-pending | **BOUNDED** (simulator disagrees with ARM) |
| 3 | Exception entry, exit and handler time | **BOUNDED** |
| 4 | Nesting and preemption | **BOUNDED** |
| 5 | PRIGROUP, PRIMASK, BASEPRI and other core interrupt controls | **DEFERRED** |
| 6 | Timer match, reset and stop timing | **RESOLVED** |
| 7 | Timer writes during counting | **BOUNDED** |
| 8 | Timer capture, external match, counter mode and PWM | **DEFERRED** |
| 9 | Clocks: PCLKSEL, PLL, PCONP | **BOUNDED** |
| 10 | ADC conversion time | **RESOLVED** |
| 11 | ADC DONE flags and the interrupt line | **RESOLVED** |
| 12 | ADC input value | **BOUNDED** |
| 13 | ADCR written during a conversion | **BOUNDED** |
| 14 | ADC burst, hardware start, overrun | **DEFERRED** |
| 15 | EINT0 edge and level behaviour | **RESOLVED** |
| 16 | EINT1–3, mode-change flags, wake-up | **DEFERRED** |
| 17 | Pin functions, pull modes and GPIO reads | **BOUNDED** |
| 18 | Which GLCD controller the board carries | **BOUNDED** |
| 19 | GLCD features not modelled | **BOUNDED** |
| 20 | The GLCD backlight on P4.28 | **BOUNDED** |
| 21 | Host adaptations for Keil's GLCD driver | **BOUNDED** |
| 22 | Firmware static state between runs in one process | **BOUNDED** |
| 23 | The interrupt-storm guard | **BOUNDED** |
| 24 | Real-hardware confirmation | **DEFERRED** |

## 1. Arbitration: RESOLVED
- **Model.** The pending, enabled exception with the lowest priority value is taken first; ties go to the lowest exception number (ARM 2.3.5: "If multiple pending exceptions have the same priority, the pending exception with the lowest exception number takes precedence"). Priorities have 5 implemented bits (`__NVIC_PRIO_BITS` = 5): a byte written as 0xFF reads 0xF8.
- **E13.** With PRIMASK set, firmware pends SysTick (priority 31), then TIMER2 (0), then TIMER0 (0), and releases PRIMASK. Handlers run TIMER0, TIMER2, SysTick: by priority, then by number, regardless of pend order. `NVIC->IP[1]` written 0xFF reads 0xF8.
- **Latasim.** `Integration.SimultaneousTimerAndSysTickAreTakenByPriority` has a TIMER0 match and SysTick's count to 0 pending on the same cycle; TIMER0 (priority 0) is taken first and SysTick (31, from `SysTick_Config`) tail-chains after it. `Nvic.*` tests pin the tie rule.

## 2. Level lines after ICPR: BOUNDED
- **ARM 4.2.9:** after a write to the clear-pending bit, "for a level-sensitive interrupt, if the interrupt signal is still asserted, the state of the interrupt does not change". The model follows this: an asserted line (timer IR bit, ADC DONE, EINT0 flag) re-pends at once.
- **E13 disagrees.** With TIMER0's IR set and its interrupt disabled, ISPR shows it pending; after writing ICPR, the simulator shows it not pending, although IR is still set.
- **Why the model keeps ARM's rule.** The ARM text is the processor vendor's statement of hardware behaviour. The simulator is a separate model of it and has disagreed with the documents before (Phase 3, SysTick reset values).
- **Impact:** none on the firmware used here. Every handler clears its source before returning, and none clears pending state in the NVIC while its source is asserted.

**To close:** repeat E13's part B on a real board.

## 3. Exception timing: BOUNDED
- **Model.** A handler runs at the virtual time of the event that made it pending, and takes no virtual time (Policy A). Entry and exit take no cycles.
- **Real hardware.** Cortex-M3 entry takes cycles for stacking and vector fetch, and handlers take instruction time. No cycle figure for LPC1768 entry is in the local documents. In E11, Blinky_ULp's first LED store lands about 114 cycles after SysTick's count to 0, and its last about 491.
- **Consequence.** Order is exact; times of handler stores are early by the handler's real duration. A handler that polls a peripheral waiting for time to pass cannot run: advancing time inside a handler is an error.
- **Sufficient for:** tick- and event-driven firmware, which is all the firmware used here.

## 4. Nesting and preemption: BOUNDED
- **Model.** Handlers do not preempt one another. An exception that becomes pending during a handler (for example a higher-priority one) is taken after the handler returns, tail-chained in priority order.
- **Why this is safe here.** Handlers take no virtual time, so nothing can become pending from elapsed time during one. Only a handler's own stores can pend another exception, and on hardware a higher-priority one would then run before the rest of the lower handler. The observable difference is the order of stores within one virtual instant.
- **Not supported:** firmware whose correctness depends on preemption inside a handler.

## 5. Core interrupt controls: DEFERRED
Not modelled: PRIGROUP (AIRCR) and priority grouping, PRIMASK / FAULTMASK / BASEPRI (host firmware cannot execute `__disable_irq`), ICSR, STIR, VTOR, SHPR1–2, fault exceptions and PendSV/SVCall. In the system control space, only SysTick, NVIC ISER/ICER/ISPR/ICPR/IABR/IPR and SHPR3 are mapped; other addresses fault as unmapped. None of the examined firmware uses them.

## 6. Timer match, reset and stop: RESOLVED
- **Model (UM10360 figures 114 and 115).** When TC equals an MR with an action enabled, the flag is set, and reset or stop happens on the following PCLK edge. With reset on match, the period is (MR + 1) × (PR + 1) PCLKs.
- **E13, at PCLK = CCLK:**
  - MR0 = 1000, interrupt only: TC read just after IR was seen is 1010. With reset on match, the same read gives 9 = 1010 − 1001. So TC holds MR0 for one clock, then resets.
  - Ten periods with MR0 = 1000 and reset: 10,010 cycles, exactly 10 × 1001.
  - Ten periods with PR = 3, MR0 = 100: 4042 cycles, against 4040 = 10 × 101 × 4. The difference is within the polling loop's jitter; per period it is not an integer.
  - Stop on match with MR0 = 50: TC holds at 50 and TCR reads 0.
- **Latasim.** The closed-form jumps are checked edge by edge against a reference stepper in `Timer.*` tests; `TimerFirmware.*` runs representative TIMER0 firmware.

## 7. Timer writes during counting: BOUNDED
- **Model.**
  - A match is found only when TC changes to the MR value. Writing TC or an MR so that they are already equal does not flag a match.
  - TCR bit 1 (reset) holds TC and PC at 0 from the write until it is cleared.
  - Writes to TC, PC, PR, MR and MCR take effect at once, between PCLK edges.
- **UM10360** says an MR acts "every time MR0 matches the TC" (table 425). It does not say whether a write can cause a match. The model takes the counting reading.
- **Impact:** none on the examined firmware, which writes these registers only while the timer is stopped or held in reset.

## 8. Timer features not modelled: DEFERRED
CCR, CR0–1, EMR and CTCR give a bus fault when accessed, as do the PWM1 registers (unmapped). Counter mode, capture inputs and external match outputs need pin modelling beyond Phase 4.

## 9. Clocks: BOUNDED
- **Core clock** is fixed at 100 MHz, the frequency Keil's MCB1700 `SystemInit` sets (Phase 3, question 8). No PLL is modelled. E13 shows the simulator's PLL0 lock interrupt (IRQ 16) pending after start-up; the model has no PLL0 and nothing pends it.
- **Peripheral clocks** come from PCLKSEL0/1 only: code 0 → CCLK/4, 1 → CCLK, 2 → CCLK/2, 3 → CCLK/8 (UM10360 table 42). The CAN exception for code 3 (CCLK/6) is not modelled; CAN is not modelled at all.
- **PCONP** is stored and read back, but gates nothing: a timer or the ADC runs with its PCONP bit clear. The examined firmware always sets the bit first. Gating would matter only for firmware that forgets the bit; TIMER0/1 are powered at reset anyway (UM10360 table 46).

## 10. ADC conversion time: RESOLVED
- **Model.** A software start takes 65 ADC clocks, each (CLKDIV + 1) PCLK_ADC cycles (UM10360 ADCR, BURST = 0: "Conversions are software controlled and require 65 clocks"). With `ADC_MCB1700.c`'s settings (PCLK_ADC = CCLK/4, CLKDIV = 4) that is 1300 core cycles.
- **E13:** same settings, start to DONE seen = 1314 cycles, which is 1300 plus polling latency comparable to part A's.

## 11. ADC DONE flags and the interrupt line: RESOLVED
- **Model.**
  - The global DONE (ADGDR bit 31) clears when ADGDR is read or ADCR is written.
  - A channel DONE clears when its ADDRn is read.
  - ADSTAT mirrors the channel DONEs, and bit 16 (ADINT) shows the interrupt line.
  - The line is the global DONE if ADINTEN bit 8 (reset value 1) is set, otherwise any enabled channel's DONE.
- **E13 matches each point.**
  - The read that saw DONE leaves ADGDR = `0x02008000` (DONE clear, channel 2, result 0x800).
  - The first ADDR2 read shows DONE, and the second does not.
  - ADSTAT is 0 after both reads. After another conversion it is `0x00010004` (DONE2, ADINT). After an ADCR write it is `0x00000004` (ADINT gone, DONE2 kept).
  - ADINTEN reads `0x100`.

## 12. ADC input value: BOUNDED
- **Model.** The pin's level is sampled when the conversion starts, as a 12-bit value from the board (`Board::set_potentiometer`). It has no noise, offset or settling. E13's 1.65 V on AD0.2 converts to 0x800, as the model's ideal transfer gives.
- **Real hardware.** A real reading of the potentiometer wobbles by a few LSBs. `ADC_MCB1700.c` reads a 12-bit result and Blinky_ULp uses only its top 4 bits (`AD_last >> 8`), so the chase speed is insensitive to this.
- **When the level is sampled** during the 65 clocks is not stated in UM10360. Sampling at the start is a choice; it matters only for inputs that change during a 13 µs conversion.

## 13. ADCR written during a conversion: BOUNDED
- **Model.** Any ADCR write clears the global DONE. A write without START = 001 leaves a running conversion to complete; a write with START = 001 and PDN restarts it on the lowest selected channel.
- **UM10360** does not say what a write during a conversion does. No examined firmware writes ADCR while a conversion is running: `ADC_MCB1700.c` starts one per SysTick tick, and each finishes 1300 cycles later.

## 14. ADC features not modelled: DEFERRED
- **Burst mode** (ADCR bit 16) and **hardware start** (START 010–111) raise `NotModelled`.
- **Overrun** exists only when a result is overwritten before being read, which in software-start mode needs a second start; overrun bits read 0.
- **ADTRM** gives a bus fault. Writes to ADGDR, ADDRn and ADSTAT are ignored.

## 15. EINT0: RESOLVED
- **Model (UM10360 3.6).**
  - EINT0 follows P2.10 only while PINSEL4[21:20] = 01.
  - In edge mode, the selected edge sets the flag, and writing 1 clears it.
  - In level mode, the flag is set while the pin is at the active level and cannot be cleared then.
  - The flag is the NVIC line.
- **E13**, with P2.10 driven from the INI:
  - After set-up (edge, falling) EXTINT reads 0.
  - A falling edge runs the handler once, and the handler's write clears EXTINT.
  - A rising edge does nothing, and a second falling edge runs it again.
  - Switching to level mode with the pin low: writing 1 to EXTINT leaves it at 1.

## 16. EINT0 extras and EINT1–3: DEFERRED
- EINT1–3's register bits are stored, but their pins (P2.11–13) are not watched.
- UM10360 3.6.3 warns that "an extraneous interrupt(s) could be set by changing the mode and not having the EXTINT cleared". The model sets one only when a level-mode setting finds the pin active, as a level would. It does not reproduce other spurious flags, because the manual does not say when they occur.
- Wake-up from power-down is not modelled.

## 17. Pin functions and GPIO: BOUNDED
- **The pin connect block** (PINSEL0–10, PINMODE0–9, PINMODE_OD0–4) is stored and read back. Reserved words read 0 and ignore writes.
- **Only EINT0** looks at PINSEL: its function select gates EINT0. The ADC does not check PINSEL1 for AD0.2, and the GLCD path does not check SSP1's pins.
- **GPIO reads** of FIOPIN return the pin's level whatever function PINSEL selects. UM10360 9.5.4 says FIOPIN gives "the logic value of the pin regardless of whether the pin is configured for input or output, or as GPIO or an alternate digital function". For pins a peripheral function drives, that peripheral's output is not modelled.
- **PINMODE** has no electrical effect: pull-ups and pull-downs come from the board model (INT0's external pull-up, the joystick's), not from the MCU.

## 18. GLCD controller identity: BOUNDED
- **Keil's `GLCD_MCB1700.c`** supports an HX8347-D, which it detects by a bit-banged ID read on P0.9, and otherwise an ILI9320-family controller, with separate initialisation sequences.
- **Which controller a given MCB1700 carries** is not stated in the pack's documents. The driver's run-time detection suggests boards exist with each.
- **The model is the ILI9320 path.** On the model the bit-banged read sees P0.9's idle level (0xFF), so the driver takes this path. In E6 the simulator also took it (reading ID 0).
- **What this means.** Output from a board with an HX8347-D panel would differ on the wire. What the driver draws at a given coordinate would not.

## 19. GLCD features not modelled: BOUNDED
- **SSP1** is not modelled as registers. The host `Driver_SPI1` (`src/firmware/spi1_host.cpp`) passes bytes straight to the display. Transfers take no virtual time; bus speed and mode are accepted and ignored.
- **Panel scan direction and mirroring** (R01, R60) are stored, not applied. `Glcd::pixel(x, y)` is in the driver's own coordinates (320 × 240, `GLCD_SWAP_XY`). A physically mirrored panel would show the same image flipped.
- **Stored but not simulated:** power, gamma and display timing registers.
- **Status reads** (RS = 0, RW = 1) are ignored.
- **GRAM at power-up** is black here; on hardware it is undefined until cleared, and every examined firmware clears the screen first.
- **The byte framing** matches E6's capture of the legacy driver on the wire (start byte, index/data, R22 bursts); Keil's current driver is checked against the model in `KeilGlcd.*`.

## 20. Backlight P4.28: BOUNDED
- **The driver.** `GLCD_Initialize` "turns the backlight on" with `GPIO_PinWrite(4, 28, 1)`, without making P4.28 an output. The model sets the latch; the pin stays an input.
- **On hardware** P4.28 would then not be driven, unless a reset default or the panel's own circuitry turns the backlight on. The backlight wiring on the board could not be confirmed from the schematic text available.
- **`KeilGlcd.InitializeTakesTheIli9320Path`** checks the latch only, and the model does not report a backlight state.

## 21. Host adaptations for the GLCD driver: BOUNDED
- **`/LARGEADDRESSAWARE:NO`** on anything linking `latasim_keil_glcd`. `GLCD_DrawChar` and `GLCD_DrawBitmap` cast pointers through `uint32_t`, which on 64-bit Windows is only safe for addresses below 2 GB. The flag keeps the image there. MSVC warns (C4311/C4312) when compiling the unchanged driver.
- **The host `Driver_SPI1`** replaces the pack's SSP driver (question 19).

## 22. Firmware static state: BOUNDED
- **Keil's drivers keep file-scope state** that host execution has no C start-up to reset, and it survives between tests in one process: Blinky_ULp's `IRQ.c` counters, `ADC_MCB1700.c`'s result, the GLCD driver's colours, font and orientation.
- **`reset_irq_statics()`** restores Blinky_ULp's, as in Phase 3, now aligning the chase through the potentiometer.
- **GLCD tests set every property they depend on** (colours, font) and clear the screen.

## 23. The interrupt-storm guard: BOUNDED
- **The guard.** If one service pass takes more than 100,000 exceptions without returning to thread mode, `logic_error` is thrown. A handler that never clears a level source would otherwise loop forever on the host.
- **The real processor** would also loop, but time would pass. In the model a handler takes no time, so the loop is infinite in zero virtual time.
- **The limit** is far above any legitimate burst in the examined firmware (a few exceptions at one instant).

## 24. Real-hardware confirmation: DEFERRED
- Every check here is against documents and µVision's simulator; no real MCB1700 was used.
- **The ones worth running on a board:** E13 parts B (ICPR with the line asserted), D (conversion time and value noise) and E (EINT0 with the real button), the GLCD controller ID, and the backlight.
