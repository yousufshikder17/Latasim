# Phase 3: firmware over deterministic virtual time

**Status: complete.**

Phase 3 lets firmware that depends on elapsed time run on the host and be tested against it:
- a virtual clock, advanced only explicitly;
- the Cortex-M3 SysTick timer on that clock;
- the firmware's `SysTick_Handler` called as the timer counts down;
- a scenario layer for writing time-based tests;
- trace events stamped with virtual time.

Keil's unmodified Blinky_ULp interrupt code runs a 10 ms LED chase with the same timing as µVision's simulator.

```powershell
scripts\build.ps1              # build and run all tests
build\latasim firmware-demo    # part 2: Blinky_ULp over 25 ms of virtual time, with the trace
```

## Architecture

```
            firmware (Keil C sources, unmodified; register-level code)
                  |
      host compatibility: Keil GPIO/PIN API, host LPC17xx.h registers,
      SystemCoreClock / SysTick_Config
                  |
      Lpc1768: MMIO (GPIO, bit-band, PCONP, SysTick) ----------------> Trace
                  |                                                    (seq, t)
      virtual time: advance_cycles(n) --> SysTick --> SysTick_Handler     ^
                  |                                                        |
      MCB1700 Board: LEDs, joystick, INT0 ---------------------------------+
                  |
      Scenario (tests): run_for / run_until, press / release, checks
```

- **Time is one integer,** core clock cycles, owned by each machine.
- **SysTick is part of the memory map** and counts those cycles.
- **Handlers run inside `advance_cycles`,** at the virtual time of each count to 0.
- **Everything is still one authoritative state.** The scenario layer only drives and observes it.

## Capabilities

| Capability | Where |
|---|---|
| **Virtual time.** `Lpc1768::cycles()`, `advance_cycles(n)`; 100 MHz core clock (`kCoreClockHz`); no wall clock anywhere | [timed-firmware.md](timed-firmware.md) |
| **SysTick.** STCTRL/STRELOAD/STCURR/STCALIB with ENABLE, RELOAD, 24-bit count, COUNTFLAG, TICKINT; checked against ARM, UM10360, E11 and E12 | [timed-firmware.md](timed-firmware.md) |
| **SysTick_Handler delivery.** Synchronous, at each count to 0 with TICKINT set; no NVIC | [timed-firmware.md](timed-firmware.md) |
| **Timed real firmware.** Keil Blinky_ULp's `IRQ.c` unchanged, plus a host port for `main()`'s start-up and a Board_ADC stub (replaced in Phase 4 by Keil's real ADC driver: [Phase 4](../phase4/overview.md)) | [timed-firmware.md](timed-firmware.md) |
| **Scenarios.** `tests/scenario.hpp`: typed GoogleTest API over virtual time | below |
| **Timed trace.** Every event has `cycles`; SysTick handler calls are events | below |

## Scenarios

`latasim::test::Scenario` (`tests/scenario.hpp`) is a thin test-facing layer. It isn't a DSL, and it doesn't replace GoogleTest.

```cpp
latasim::test::reset_irq_statics();          // Blinky_ULp only: fresh IRQ.c state
Scenario s;                                  // new board, bound to host firmware, t = 0
blinky_ulp_start();
Joystick_Initialize();
s.on_systick(SysTick_Handler);               // since Phase 4: s.bind(kSysTickIrq, SysTick_Handler)

s.run_until(10ms);
EXPECT_TRUE(s.led(1, LedState::On));
s.run_until(25ms);
s.press(JoystickDirection::Up);              // at t = 25 ms
EXPECT_TRUE(s.pin(1, 23, Level::Low));
EXPECT_EQ(Joystick_GetState(), JOYSTICK_UP);
s.run_until(50ms);
s.release(JoystickDirection::Up);
s.run_until(80ms);
EXPECT_TRUE(s.led(0, LedState::On));
EXPECT_TRUE(s.reg(0x2009C034, 0x5FFFC713));  // FIO1PIN
```

**Time and inputs:**
- **Units.** Times are `std::chrono` durations used purely as units, converted exactly to cycles. `ms` and `us` convert implicitly; `ns` does not compile, because 1 ns is 0.1 cycle.
- **`run_for(d)`** advances by a duration. A negative duration throws.
- **`run_until(t)`** advances to an absolute time. A time already passed throws.
- **Inputs** happen at the current virtual time, so a timed input is `run_until(t)` followed by `press`/`release`.

**Checks:**
- **The three checks** are `led(i, state)`, `pin(port, pin, Level)` and `reg(address, value)`. Each returns a GoogleTest `AssertionResult`.
- **They observe without side effects.** They make no MMIO access and record no trace event, and `reg` reads STCTRL without clearing COUNTFLAG (via `Lpc1768::peek32`).
- **A failure shows the expected and actual values, the virtual time and the last six trace events:**

```
LED0 is ON, expected OFF
  at t = 250 cycles (0.002500 ms)
  last trace events:
    ...
    #33   t=0          write32 FIO1SET   0x10000000
    #34   t=0          led     LED0      ON
```

## Timed trace

**Every event has `cycles`,** the virtual time at which it happened, stamped by the machine.
- **Order.** The sequence number stays the exact order. Events with no time between them share a time, in sequence order.
- **Handler calls.** A `SysTick` event marks each `SysTick_Handler` call, before the handler's own accesses.
- **Determinism.** Times and sequence numbers are monotonic, and a new machine restarts both. Identical runs give identical traces, and recording on or off gives identical behaviour (all tested).

From `latasim firmware-demo`, part 2:

```
run for 25 ms (2,500,000 cycles)
    #38   t=999999     systick handler
    #39   t=999999     write32 FIO1CLR   0x10000000
    #40   t=999999     write32 FIO1SET   0x20000000
    #41   t=999999     led     LED1      ON
    ...
    #48   t=1999999    systick handler
    ...
    #52   t=1999999    write32 FIO1SET   0x80000000
    #53   t=1999999    led     LED2      ON
```

## Validation against the simulator (B3)

| Experiment | What | Result |
|---|---|---|
| E11 | Keil's stock Blinky_ULp: SysTick reset values, count timing, LED stores per tick | First count to 0 999,999 cycles after `SysTick_Config`, then every 1,000,000, and the same LED order and store order as Latasim (after fixing the first-period rule). Reset values differ from UM10360 (bounded) |
| E12 | SysTick register semantics from firmware | ENABLE loads RELOAD even mid-count; COUNTFLAG clears on read; writing STCURR clears it; disabling holds. All as modelled |

Both experiments are scripted and ran twice with identical output. The test suite never needs µVision.

**Open and bounded items:** [open-questions.md](open-questions.md).

## Not supported

- **Interrupts and timing:** NVIC, interrupt priorities, pending and preemption, exception entry and exit time, and any interrupt other than SysTick.
- **Cycle accuracy:** host-executed firmware takes no virtual time, and there is no CPU instruction execution (B4).
- **Clocks:** the clock tree (PLL, dividers). The core clock is fixed at 100 MHz, and the SysTick STCLK source is not modelled.
- **Peripherals:** timers 0–3, RIT, ADC (Blinky_ULp's is a stub), GLCD, UART, USB, Ethernet, CAN, I2C, audio. Phase 4 adds the NVIC, timers 0–3, the ADC, EINT0 and the GLCD ([Phase 4](../phase4/overview.md)).
- **Tooling:** replay, trace files, VCD, scenario files, Qt UI.
- **Trace memory:** the trace grows without bound; long runs keep every event in memory.

## Tests

182 in total, all passing. 23 of them compile Keil's sources and are built only when the packs are installed. Phase 3 added 50:

| Suite | Tests |
|---|---|
| `VirtualTime` | 5 |
| `SysTick` | 17 |
| `SysTickHandler` | 7 |
| `Scenario` (the layer itself) | 8 |
| `Trace` (time additions) | 4 |
| `HostRegisters` (`SysTick_Config`) | 1 |
| `BlinkyUlp` (packs) | 7 |
| `KeilScenario` (packs) | 1 |
