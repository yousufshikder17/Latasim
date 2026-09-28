# Phase 3: virtual time, SysTick and timed firmware

## Virtual time

- **Unit.** One integer unit: core clock cycles (`uint64`), per `Lpc1768`.
  - It starts at 0 and moves only through `advance_cycles(n)`.
  - Running firmware on the host takes no virtual time.
  - Nothing reads a wall clock, sleeps, uses OS timers or starts threads.
- **Core clock.** `lpc17xx::kCoreClockHz` = 100 MHz, one named constant: `virtual cycles = core clock cycles = SysTick clocks (CLKSOURCE = 1)`.
  - Keil's MCB1700 `SystemInit` configures this: PLL0 from the 12 MHz crystal, M = 100, N = 6, CCLK divider 4.
  - The LPC1768 simulator reports it (E2), and STCALIB's 10 ms value assumes it.
  - There is no clock-tree model: firmware that reprograms the PLL does not change it.
  - The host's `SystemCoreClock` is the same constant, and `SystemCoreClockUpdate()` does nothing.

## SysTick

**STCTRL, STRELOAD, STCURR and STCALIB** at `0xE000E010`–`0xE000E01C` are part of the `Lpc1768` memory map, reached through the same traced loads and stores as GPIO. Sources: ARM DUI 0552A §4.4 (in the device pack), UM10360 chapter 23, E11 (the stock Blinky_ULp in the simulator) and E12 (SysTick register semantics from firmware in the simulator).

| Behaviour | Model | Source |
|---|---|---|
| Reset | STCTRL `0x4` (CLKSOURCE = CPU clock), STRELOAD 0, STCURR 0, STCALIB `0x000F423F` | UM10360 Table 438; the simulator reads STCTRL and STCALIB as 0 ([open-questions.md](open-questions.md) §4) |
| ENABLE 0→1 | STCURR ← RELOAD at once, whatever it held | ARM 4.4.1; E11, E12 |
| Each clock while enabled | STCURR = 0 wraps to RELOAD; otherwise decrements; 1→0 sets COUNTFLAG | ARM 4.4 |
| Resulting period | After `SysTick_Config(N)`: first count to 0 after N−1 clocks, then every N | E11: 999,999 then 1,000,000 |
| RELOAD 0 | Never counts to 0 | ARM 4.4.2 |
| Read STCTRL | Returns COUNTFLAG (bit 16) and clears it; reserved bits read 0 | ARM 4.4.1; E12 |
| Write STCTRL / STRELOAD | Keeps ENABLE, TICKINT, CLKSOURCE / keeps 24 bits | ARM 4.4 |
| Write STCURR | Any value clears the counter and COUNTFLAG | ARM 4.4.3; E12 |
| CLKSOURCE = 0 (STCLK pin) | The counter holds: no STCLK clock is modelled | UM10360 23.1 |
| Write STCALIB, narrow or misaligned access, other SCS addresses | `BusFault` | ARM says STCALIB is read-only, UM10360 says R/W |

**Counting is closed-form,** so advancing by millions of cycles costs nothing. It is tested against a clock-by-clock reference.

## SysTick_Handler

All real timed MCB1700 firmware does its work in `SysTick_Handler`: Keil's Blinky_ULp, and the joystick/LED demo firmware. None of it polls COUNTFLAG. So the one exception Latasim delivers is SysTick, in the narrowest form that runs it:

- **`Lpc1768::on_systick(handler)` attaches the firmware's handler.** While one is attached and TICKINT is set, `advance_cycles` stops at each count to 0 and calls it there. Its register accesses happen at that virtual time.
- **Calls are synchronous,** between host firmware calls.
- **Not modelled:** NVIC, priorities, pending state, preemption, nesting, exception entry and exit cycles.
  - The handler takes no virtual time. In the simulator its LED stores land 114–491 cycles after the count to 0 (E11).
- **Advancing time from inside the handler** is an error.
- **Without a handler, or with TICKINT clear,** TICKINT is only stored.

**The host `SysTick_Config`** (`src/host/cmsis_system.cpp`) makes CMSIS's stores (STRELOAD = ticks−1, STCURR = 0, STCTRL = 7). It omits `NVIC_SetPriority`, because priorities are not modelled.

## Timed firmware: Keil Blinky_ULp

**Source:** `Boards/Keil/MCB1700/Blinky_ULp` in `Keil::LPC1700_DFP` 2.6.0 (BSD-3-Clause, Arm).
- `Blinky.c`'s `main()` calls `LED_Initialize`, `ADC_Initialize`, `SystemCoreClockUpdate` and `SysTick_Config(SystemCoreClock/100)`, then loops forever reading the ADC.
- `IRQ.c`'s `SysTick_Handler` runs every 10 ms: it sets a one-second flag every 100 ticks and steps an LED chase whenever `AD_last >> 8` ticks have passed.

**Compiled unchanged, as C, from the installed pack** (library `latasim_keil_blinky_ulp`):
- `IRQ.c`;
- Keil's LED driver, as in Phase 2.

**Adaptations** (`src/firmware/blinky_ulp.c`):

| Part | Why | Stand-in |
|---|---|---|
| `Blinky.c` | its `main()` never returns | `blinky_ulp_start()` runs `main()`'s four start-up calls; `AD_last` is defined there |
| `Blinky.c`'s loop | it only reads the ADC and prints once a second | not run; a test reading `clock_1s` plays its part |
| `ADC_MCB1700.c` | the ADC and its interrupt are not modelled | `Board_ADC.h` stub: no conversion ever completes, so `AD_last` stays 0 |
| Startup, `system_LPC17xx.c` | target-only | host `SystemCoreClock`, `SysTick_Config` |

**With `AD_last` = 0, the chase steps on every tick.** `AD_last` also reads 0 at the end of E11, so the stock firmware in the simulator steps on every tick as well.

**Execution model:** `blinky_ulp_start()`, then `board.mcu().on_systick(SysTick_Handler)`, then `advance_cycles(...)`.

**Behaviour** (`tests/keil_timed_firmware_test.cpp`):

| Virtual time | Firmware | Board |
|---|---|---|
| 0 | start-up: LED pins driven low, SysTick 10 ms with TICKINT | all LEDs off |
| 999,999 cycles (tick 1) | `LED_SetOut(0x02)` | LED1 on |
| + 1,000,000 per tick | the next step | LED *n* mod 8 on at tick *n*, wrapping after LED7 |
| tick 100 | `clock_1s = 1` | |

**Tick trace.** Each tick's trace is `LED_SetOut`'s eight SET/CLR stores, in LED order, each followed by the LED change it causes. For example, tick 2: `FIO1CLR` LED0, `FIO1CLR` LED1 → LED1 OFF, `FIO1SET` LED2 → LED2 ON, `FIO2CLR` LED3–LED7.

**Repeated runs** give the same trace, GPIO state, LED state and virtual time. `IRQ.c` keeps its state in function statics, which nothing resets within one process. So a test that runs the firmware twice uses whole 2 s spans (200 ticks), which return that state to where it started.

**B3 comparison (E11), with the stock firmware in the simulator:**
- the same first tick (999,999 cycles after `SysTick_Config`);
- the same period (1,000,000);
- the same LED order and the same order of stores within a tick.

Only the handler's own execution time differs.
