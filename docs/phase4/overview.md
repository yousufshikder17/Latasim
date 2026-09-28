# Phase 4: interrupts and peripherals

**Status: complete.**

Phase 4 turns the single SysTick callback of Phase 3 into general interrupt delivery, and adds the peripherals MCB1700 firmware uses most:
- an NVIC model: enable, pending, active and priority state per exception, arbitration, and SysTick delivered through it;
- TIMER0–3 (match, reset, stop, prescale, peripheral clock);
- the ADC, with the board's potentiometer on AD0.2;
- the INT0 button as a real EINT0 external interrupt;
- the MCB1700's graphic LCD, driven by Keil's own GLCD driver;
- all of them in one scenario, competing for one NVIC.

Keil's Blinky_ULp now runs with its real ADC driver: the chase speed follows the potentiometer, as on the board, instead of Phase 3's stub.

```powershell
scripts\build.ps1                                                # build and run all tests
build\latasim firmware-demo                                      # part 2: SysTick and ADC interrupts in the trace
build\tests\latasim_keil_firmware_tests --gtest_filter=Integration.*   # the integrated scenario
```

## Architecture

```
      firmware (Keil C sources unchanged; register-level code)
          |                                    |
   host compatibility: LPC17xx.h proxies,      handlers bound by IRQ number
   CMSIS NVIC_* / SysTick_Config,              (bind_handler), main-loop step
   Keil GPIO/PIN API, Driver_SPI1              (on_thread_mode)
          |                                    ^
   Lpc1768 MMIO: GPIO, PINCON, PCLKSEL, PCONP, |
   SysTick, TIMER0-3, ADC, EXTINT, NVIC        |
          |                                    |
   virtual time: advance_cycles(n) steps to the next event
   (SysTick to 0, a timer match, an ADC completion)
          |                                    |
   interrupt lines --> Nvic (pending/active/priority) --> dispatch
          |
   MCB1700 Board: LEDs, joystick, INT0 (P2.10), potentiometer (AD0.2),
                  GLCD (chip select P0.6)                 ---> Trace (seq, t)
```

- **One machine state.** Peripherals raise interrupt lines; the NVIC turns lines and software writes into pending state; the machine dispatches bound handlers.
- **Time moves only in `advance_cycles`**, from event to event, never by wall clock. Timer and ADC progress is computed in closed form, not cycle by cycle.
- **The model never names firmware symbols.** The host binds a handler to an IRQ number, as a vector table would.

## Interrupt delivery

| Aspect | Model |
|---|---|
| **Exceptions** | SysTick (IRQn −1) and external IRQs 0–34 (TIMER0–3 = 1–4, EINT0 = 18, ADC = 22), by CMSIS IRQ number |
| **Registers** | ISER/ICER/ISPR/ICPR/IABR 0–1 (word only), IPR0–8 (byte, halfword, word), SHPR3 (SysTick priority, byte `0xE000ED23`) |
| **Priority** | 5 implemented bits (`__NVIC_PRIO_BITS` = 5): stored as `value & 0xF8` |
| **Arbitration** | Lowest priority value first; ties to the lowest exception number (SysTick = 15, IRQn + 16) |
| **Lines** | Level: a line pends while asserted and the exception isn't active; re-pends on exit if still asserted, and at once after ICPR if still asserted (ARM 4.2.9) |
| **Dispatch** | A handler runs synchronously at the virtual time of the event, taking no virtual time (Policy A). No nesting: exceptions pending during a handler tail-chain after it, in priority order. An exception without a bound handler stays pending |
| **Thread mode** | `on_thread_mode(step)` runs after the processor returns from exceptions: the stand-in for a main loop's reaction to flags set by handlers |
| **When** | After each time step, MMIO store (outside handlers), input change and `bind_handler` |
| **Guard** | More than 100,000 exceptions in one service pass throws `logic_error` (a handler that never clears its source) |

**SysTick** is now an exception like any other. Counting to 0 with TICKINT set pends it, and CMSIS `SysTick_Config` sets its priority to 31 (the lowest), as the real one does. Phase 3's direct SysTick callback is gone.

**Host CMSIS** (`src/host/cmsis_system.cpp`, C linkage, all through MMIO): `NVIC_EnableIRQ`, `NVIC_DisableIRQ`, `NVIC_SetPendingIRQ`, `NVIC_ClearPendingIRQ`, `NVIC_GetPendingIRQ`, `NVIC_GetActive`, `NVIC_SetPriority`, `NVIC_GetPriority`, `SysTick_Config`, `SystemCoreClock`, `SystemCoreClockUpdate`. The host `LPC17xx.h` has the `IRQn_Type` values used here and proxies for `LPC_SC`, `LPC_PINCON`, `LPC_TIM0`–`3` and `LPC_ADC`.

## Timers

TIMER0–3 at `0x40004000`, `0x40008000`, `0x40090000`, `0x40094000` (IRQs 1–4):
- **Registers:** IR (write 1 to clear), TCR (enable, reset), TC, PR, PC, MCR and MR0–3.
- **Counting:** TC counts PCLK edges after PC reaches PR. PCLK comes from PCLKSEL0/1 (codes 0–3 → CCLK/4, CCLK, CCLK/2, CCLK/8), and edges fall on multiples of the divider.
- **Matches:** a match with an MCR action enabled sets the IR bit, and reset or stop happens one PCLK later (UM10360 figures 114 and 115). With reset on match, the period is (MR + 1) × (PR + 1) PCLKs, as E13 measured.
- **Not modelled:** capture, external match outputs and counter mode. CCR, CR0–1, EMR and CTCR give a bus fault.
- **Timer events** (a match that sets IR bits) are traced as `match TIMER0 MR0`.

## ADC and the potentiometer

The ADC at `0x40034000` (IRQ 22):
- **Registers:** ADCR (reset `0x01`), ADGDR, ADINTEN (reset `0x100`), ADDR0–7 and ADSTAT.
- **Conversions:** START = 001 with PDN converts the lowest selected channel in 65 ADC clocks of PCLK_ADC / (CLKDIV + 1). Keil's settings make that 1300 core cycles; E13 measured 1314 including polling. The input is sampled when the conversion starts.
- **DONE flags:** the global DONE clears on an ADGDR read or any ADCR write, and a channel DONE clears on its ADDRn read.
- **Interrupt line:** the global DONE when ADINTEN bit 8 is set, otherwise the enabled channels' DONEs. ADSTAT bit 16 shows it.
- **Not modelled:** burst mode and hardware starts raise `NotModelled`; ADTRM gives a bus fault.
- **The board** drives AD0.2 from the potentiometer: `Board::set_potentiometer(raw12)`.

**Blinky_ULp with its real ADC.** Keil's `ADC_MCB1700.c` is compiled unchanged, and Phase 3's `Board_ADC` stub is removed:
- `SysTick_Handler` starts a conversion every tick.
- `ADC_IRQHandler` stores the result 1300 cycles later.
- The thread-mode step copies it to `AD_last`, as `Blinky.c`'s loop does.
- The chase then steps every `(AD_last >> 8) + 1` ticks.

`KeilAdc.BlinkyChaseSpeedFollowsThePotentiometer` and `KeilAdc.TurningThePotentiometerMidRunChangesTheSpeed` show the speed following the potentiometer.

## INT0 as EINT0

- **Pins and registers.** The INT0 button drives P2.10. The pin connect block (`0x4002C000`–`0x4002C07C`: PINSEL, PINMODE, PINMODE_OD) is stored, and EINT0 sees P2.10 only while PINSEL4[21:20] = 01. EXTINT, EXTMODE and EXTPOLAR are at `0x400FC140`–`0x400FC14C`.
- **Edge mode** sets the flag on the selected edge, and writing 1 clears it.
- **Level mode** (the reset state, low-active) sets the flag while the pin is active, and it cannot be cleared then.
- **The flag is IRQ 18's line.** E13 matched each behaviour.
- **Keil's `PIN_Configure`** now performs its real read-modify-writes on PINSEL, PINMODE and PINMODE_OD.

The representative firmware (`tests/host/eint0_counter.cpp`) follows UM10360 3.6's procedure: select the pin function, set mode and polarity with the interrupt disabled, clear EXTINT, enable in the NVIC. The Keil packs have no EINT0 example.

## GLCD

The MCB1700's 240 × 320 TFT is modelled as an ILI9320-family controller on SPI, with chip select on P0.6 (`src/boards/mcb1700/glcd.hpp`):
- **Protocol:** a start byte `0x70 | RS << 1 | RW`, then 16-bit words, most significant byte first, for index writes, register writes, GRAM bursts through R22, and reads after one dummy byte (R00 reads `0x9320`).
- **Addressing:** GRAM writes follow entry mode R03 (I/D, AM) inside the window R50–R53, from the address R20/R21 set.
- **Coordinates:** `Glcd::pixel(x, y)` is in the driver's own coordinates (320 × 240 with `GLCD_SWAP_XY`), in RGB565. `Glcd::hash()` fingerprints the screen.
- **Trace:** register writes show as `glcd R07 0x0137`, and each GRAM burst as `glcd GRAM 384 px` when chip select is released.

**Keil's `GLCD_MCB1700.c` and `GLCD_Fonts.c` run unchanged.** They are tested in `KeilGlcd.*` for initialisation, clearing, pixels, rectangles, and characters bit for bit against the font. They reach the display through two host pieces:
- **A host `Driver_SPI1`** (`src/firmware/spi1_host.cpp`), the CMSIS-Driver interface the driver calls, in place of the pack's SSP driver. It passes bytes to the display, drives chip select through the modelled GPIO registers as Keil's SSP driver does, and takes no virtual time.
- **`/LARGEADDRESSAWARE:NO`** on executables that link the driver, because `GLCD_DrawChar` and `GLCD_DrawBitmap` cast pointers through `uint32_t`.

The driver's bit-banged controller-ID read on P0.9 sees 0xFF, so it takes the ILI9320 path. Which controller a particular board carries, mirroring, backlight wiring and what is not modelled: [open-questions.md](open-questions.md) 18–21.

## The integrated scenario

`tests/keil_integration_test.cpp` runs one board with:
- Keil's Blinky_ULp (SysTick at priority 31 from `SysTick_Config`, the ADC at its reset priority 0, with Keil's handlers);
- Keil's GLCD driver;
- the EINT0 button counter;
- a TIMER0 interrupt.

The main loop is `Blinky.c`'s ADC step plus drawing the INT0 press count with `GLCD_DrawString`.
- **`SimultaneousTimerAndSysTickAreTakenByPriority`:** a TIMER0 match and SysTick's count to 0 pend on the same cycle (999,999). TIMER0 (priority 0) is taken first, SysTick tail-chains after it, and SysTick's ADC conversion interrupts 1300 cycles later.
- **`ButtonTimerAdcSysTickAndDisplayTogether`:** 100 ms with two presses. The counts, the chase position at potentiometer 0x300, and the text drawn on the display are all checked.
- **`RepeatedRunsAreIdentical`:** trace and display, run twice.

A press at 25 ms, from the trace:

```
#649  t=2001299    adc     AD0.2     0x300
#650  t=2001299    irq     ADC       pend
#651  t=2001299    irq     ADC       enter
#652  t=2001299    read32  ADSTAT    0x00010004
#653  t=2001299    read32  ADGDR     0x82003000
#654  t=2001299    irq     ADC       exit
#655  t=2500000    input   P2.10     low
#656  t=2500000    irq     EINT0     pend
#657  t=2500000    irq     EINT0     enter
#658  t=2500000    write32 EXTINT    0x00000001
#659  t=2500000    write32 FIO2SET   0x00000040
#660  t=2500000    led     LED7      ON
#661  t=2500000    irq     EINT0     exit
#662  t=2500000    write32 FIO0CLR   0x00000040
...
#665  t=2500000    glcd    R50       0x0008
...
#696  t=2500000    glcd    GRAM      384 px
```

The handler runs first; the display update comes from the thread-mode step after it returns.

## Trace

Phase 4 adds these event kinds to Phase 3's reads, writes, inputs and LED changes:

| Kind | Example | Meaning |
|---|---|---|
| Interrupt | `irq SysTick pend` / `enter` / `exit` | pending state set, handler entered, handler returned |
| TimerMatch | `match TIMER0 MR0` | a match set IR bits |
| AdcConversion | `adc AD0.2 0x300` | a conversion finished with this result |
| Display | `glcd R03 0x1038`, `glcd GRAM 76800 px` | a controller register write; a GRAM burst's pixel count |

Register names now cover the NVIC (`ISER0`, `IPR5`, `PRI_22`, `SHPR3`, `PRI_15`), timers (`T0IR` …), the ADC, `PCLKSEL0/1`, `EXTINT`/`EXTMODE`/`EXTPOLAR`, and `PINSELn`/`PINMODEn`/`PINMODEODn`. Board input events are recorded before the pin is driven, so the interrupts they cause follow them.

## Host adaptations

Everything from the packs is compiled unchanged at build time; nothing from them is committed.

| Piece | Why |
|---|---|
| `src/firmware/blinky_ulp.c` | `Blinky.c`'s `main()` never returns. The port runs its start-up (`blinky_ulp_start`) and its loop's first two statements as the thread-mode step (`blinky_ulp_main_loop_step`); the rest of the loop only averages and prints |
| `src/firmware/adc_mcb1700.cpp` | Includes `ADC_MCB1700.c` to compile it as C++, since it accesses registers directly and host register proxies need C++; declares its interfaces `extern "C"` first |
| `src/firmware/spi1_host.cpp` | Host `Driver_SPI1` for the GLCD driver; SSP1 is not modelled as registers |
| `/LARGEADDRESSAWARE:NO` | The GLCD driver's `uint32_t` pointer casts |
| `src/host/keil_rte_gpio.cpp` | `PIN_Configure` performs the pack driver's register writes |

## Supported and not supported

**Supported:**
- NVIC enable, pending, active and priority, with arbitration and tail-chaining;
- SysTick, TIMER0–3, the ADC (software start), EINT0;
- PCLKSEL clocks and the pin connect block;
- the GLCD through Keil's driver;
- multiple interrupt sources in one deterministic run.

**Not supported:**
- preemption and nesting;
- exception entry and handler time;
- PRIGROUP, PRIMASK and BASEPRI;
- timer capture, external match, counter mode and PWM;
- ADC burst mode and hardware start;
- EINT1–3;
- SSP1 registers and SPI timing;
- the HX8347-D GLCD path;
- PLL and clock changes, and PCONP gating;
- UART, RIT, DMA, USB, Ethernet, CAN, I2C and audio;
- any CPU emulation.

Details and evidence: [open-questions.md](open-questions.md).

## Tests

- **267 tests** with the Keil packs installed; all pass on a clean build.
  - 230 need no packs.
  - The other 37 compile Keil sources: the board drivers, GPIO driver, Blinky_ULp and ADC driver, and GLCD driver.
- **The GLCD tests** (10 of the 37) also need the ARM CMSIS pack's `Driver_SPI.h`. Without it they are left out and the rest still build.
- **New in Phase 4:** `Nvic.*`, `Timer.*`, `TimerFirmware.*`, `Adc.*`, `KeilAdc.*`, `Eint.*`, `Glcd.*`, `KeilGlcd.*`, `Integration.*`. The SysTick, trace, scenario and host tests are updated for interrupt delivery.

## Evidence

**E13** (`spikes/uvsim-script/e13-phase4.ini`, `e13-phase4-firmware.c`) runs firmware in µVision's LPC1768 simulator. It measured:
- timer match, reset and stop timing;
- NVIC arbitration and priority bits;
- a level line after ICPR;
- ADC conversion time, DONE/ADINT semantics and a 1.65 V conversion;
- EINT0 edges and level mode.

Everything agrees with the model, except that the simulator does not re-pend an asserted line after ICPR. ARM documents that it does, and the model follows ARM: [open-questions.md](open-questions.md) 2.
